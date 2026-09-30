/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#include "twai_node.h"

#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"

namespace espos_n2k {
namespace detail {

namespace {
constexpr const char* kTag = "twai_node";

/// What the ISR hands the task. A classic CAN frame is small enough to copy
/// by value into a queue, which is what keeps the ISR short.
struct RxItem {
  CanMessage msg;
};
}  // namespace

TwaiNode& TwaiNode::instance() {
  static TwaiNode node;
  return node;
}

esp_err_t TwaiNode::acquire(const TwaiNodeConfig& config) {
  if (!lock_) {
    lock_ = xSemaphoreCreateMutex();
    if (!lock_) return ESP_ERR_NO_MEM;
  }
  xSemaphoreTake(lock_, portMAX_DELAY);

  if (node_) {
    // Already up. Joining with different pins would mean one of the two
    // callers is talking to a bus it did not configure; say so rather than
    // pretend.
    if (config.tx_pin != GPIO_NUM_NC &&
        (config.tx_pin != config_.tx_pin || config.rx_pin != config_.rx_pin ||
         config.bitrate != config_.bitrate)) {
      ESP_LOGW(kTag,
               "bus already up on TX=%d RX=%d %ukbps — ignoring the new config",
               (int)config_.tx_pin, (int)config_.rx_pin,
               (unsigned)(config_.bitrate / 1000));
    }
    refs_.fetch_add(1);
    xSemaphoreGive(lock_);
    return ESP_OK;
  }

  // Say so rather than binding pin -1 and reporting success: an application
  // that forgot to set the pins gets one clear line instead of a silent bus
  // that never receives anything.
  if (config.tx_pin == GPIO_NUM_NC || config.rx_pin == GPIO_NUM_NC) {
    ESP_LOGE(kTag, "tx_pin/rx_pin not set — TWAI not started");
    xSemaphoreGive(lock_);
    return ESP_ERR_INVALID_ARG;
  }

  config_ = config;

  rx_queue_ = xQueueCreate(config.rx_queue_depth, sizeof(RxItem));
  if (!rx_queue_) {
    xSemaphoreGive(lock_);
    return ESP_ERR_NO_MEM;
  }

  /* Room for everything the driver can hold at once, which is its software
   * queue PLUS whatever is already in a hardware TX buffer: a queued frame is
   * popped into a hardware slot and only acknowledged when that slot
   * completes, so both counts are live at the same moment. A ring the size of
   * the queue alone would make US the tighter limit and refuse frames the
   * driver would have taken.
   *
   * The hardware count is TWAI_HAL_TX_BUFFER_SLOT_NUM, which lives in a
   * private HAL header and is reported by no public API, so it is spelled out.
   * It is the HAL's ceiling rather than this SoC's number, which is the safe
   * direction: the ring is generous, never short. Were a future HAL to raise
   * that ceiling the only consequence is the ESP_ERR_NO_MEM this function
   * already returns safely -- never a slot handed out twice. */
  constexpr size_t kMaxHwTxSlots = 8; /* TWAI_HAL_TX_BUFFER_SLOT_NUM */
  const size_t slots = config.tx_queue_depth + kMaxHwTxSlots;
  tx_slots_ = static_cast<TxSlot*>(calloc(slots, sizeof(TxSlot)));
  tx_in_use_ = static_cast<bool*>(calloc(slots, sizeof(bool)));
  if (!tx_slots_ || !tx_in_use_) {
    teardown();
    xSemaphoreGive(lock_);
    return ESP_ERR_NO_MEM;
  }
  tx_ring_.init(tx_in_use_, slots);

  twai_onchip_node_config_t node_cfg = {};
  node_cfg.io_cfg.tx = config.tx_pin;
  node_cfg.io_cfg.rx = config.rx_pin;
  node_cfg.io_cfg.quanta_clk_out = GPIO_NUM_NC;
  node_cfg.io_cfg.bus_off_indicator = GPIO_NUM_NC;
  node_cfg.bit_timing.bitrate = config.bitrate;
  node_cfg.tx_queue_depth = config.tx_queue_depth;
  // The peripheral timestamps received frames when this is non-zero, but the
  // rest of espOS measures in esp_timer microseconds since boot and the two
  // are not the same clock. Left disabled; the task stamps on arrival, as the
  // old twai_receive() loop did.
  node_cfg.timestamp_resolution_hz = 0;

  esp_err_t err = twai_new_node_onchip(&node_cfg, &node_);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "twai_new_node_onchip failed: %s", esp_err_to_name(err));
    teardown();
    xSemaphoreGive(lock_);
    return err;
  }

  /* on_tx_done is what makes the slot ring work: it names the frame the driver
   * has finished with, so a slot is returned exactly when the driver stops
   * reading it. Not in IRAM -- espOS does not set CONFIG_TWAI_ISR_CACHE_SAFE,
   * and registration fails loudly with ESP_ERR_INVALID_ARG if a consumer does,
   * rather than misbehaving quietly. */
  const twai_event_callbacks_t cbs = {
      .on_tx_done = &TwaiNode::on_tx_done,
      .on_rx_done = &TwaiNode::on_rx_done,
      .on_state_change = &TwaiNode::on_state_change,
      .on_error = &TwaiNode::on_error,
  };
  err = twai_node_register_event_callbacks(node_, &cbs, this);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "twai_node_register_event_callbacks failed: %s",
             esp_err_to_name(err));
    teardown();
    xSemaphoreGive(lock_);
    return err;
  }

  task_running_.store(true);
  if (xTaskCreate(&TwaiNode::rx_task, "twai_rx", 4096, this, 5, &task_) !=
      pdPASS) {
    task_running_.store(false);
    ESP_LOGE(kTag, "could not start the twai task");
    teardown();
    xSemaphoreGive(lock_);
    return ESP_ERR_NO_MEM;
  }

  err = twai_node_enable(node_);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "twai_node_enable failed: %s", esp_err_to_name(err));
    // Let the task go before deleting the node it holds a pointer to: it
    // wakes at least every 100 ms and may be about to call into the driver.
    task_running_.store(false);
    for (int i = 0; i < 20 && task_; i++) vTaskDelay(pdMS_TO_TICKS(20));
    teardown();
    xSemaphoreGive(lock_);
    return err;
  }

  refs_.store(1);
  ESP_LOGI(kTag, "TWAI started: TX=%d RX=%d %ukbps", (int)config.tx_pin,
           (int)config.rx_pin, (unsigned)(config.bitrate / 1000));
  xSemaphoreGive(lock_);
  return ESP_OK;
}

void TwaiNode::release() {
  if (!lock_) return;
  xSemaphoreTake(lock_, portMAX_DELAY);
  if (refs_.load() <= 0) {
    xSemaphoreGive(lock_);
    return;
  }
  if (refs_.fetch_sub(1) != 1) {  // somebody else is still using the bus
    xSemaphoreGive(lock_);
    return;
  }

  if (node_) twai_node_disable(node_);
  task_running_.store(false);
  // The task wakes at least every 100 ms on its queue read and then exits.
  for (int i = 0; i < 20 && task_; i++) vTaskDelay(pdMS_TO_TICKS(20));
  if (task_) {
    // It did not exit in 400 ms, and teardown() is about to delete the queue
    // it is blocked on -- vQueueDelete under a waiting reader is a
    // use-after-free, not a clean wakeup. Leaking the queue is the lesser
    // fault: it is one allocation on a path taken at shutdown, and the
    // alternative corrupts memory on a device that is still running.
    ESP_LOGE(kTag,
             "rx task still running after 400 ms — leaking its queue rather "
             "than freeing it underneath it");
    node_ = nullptr; /* twai_node_delete would fault the same way */
    rx_queue_ = nullptr;
    /* The ring leaks with it, and must: the node is never deleted on this
     * path, so the driver may still hold a pointer into it for ever. */
    tx_ring_.init(nullptr, 0);
    tx_slots_ = nullptr;
    tx_in_use_ = nullptr;
    refs_.store(0);
    xSemaphoreGive(lock_);
    return;
  }
  teardown();
  ESP_LOGI(kTag, "TWAI stopped");
  xSemaphoreGive(lock_);
}

/// Caller holds lock_ (or is on the failure path of acquire()).
void TwaiNode::teardown() {
  /* The node first, always. Freeing the slots while the driver still held a
   * queued pointer would be the very bug this ring exists to prevent, moved to
   * shutdown -- and harder to see, because nothing transmits afterwards. */
  if (node_) {
    twai_node_delete(node_);
    node_ = nullptr;
  }
  tx_ring_.init(nullptr, 0);
  free(tx_slots_);
  tx_slots_ = nullptr;
  free(tx_in_use_);
  tx_in_use_ = nullptr;
  if (rx_queue_) {
    vQueueDelete(rx_queue_);
    rx_queue_ = nullptr;
  }
  refs_.store(0);
}

void TwaiNode::set_sink(FrameSink sink, void* ctx) {
  sink_ctx_.store(ctx);
  sink_.store(sink);
}

size_t TwaiNode::tx_slot_index(const twai_frame_t* frame) const {
  /* Matched by address against the slots we own: the pointer came back from
   * the driver, and a linear scan also rejects a frame that is somehow not one
   * of ours instead of computing an index into the middle of the ring. */
  for (size_t i = 0; i < tx_ring_.size(); i++) {
    if (&tx_slots_[i].frame == frame) return i;
  }
  return TxSlotRing::kNone;
}

void TwaiNode::release_tx_slot(const twai_frame_t* frame) {
  if (!frame) return;
  portENTER_CRITICAL_SAFE(&tx_mux_);
  const size_t idx = tx_slot_index(frame);
  if (idx != TxSlotRing::kNone) tx_ring_.release(idx);
  portEXIT_CRITICAL_SAFE(&tx_mux_);
}

bool TwaiNode::on_tx_done(twai_node_handle_t node,
                          const twai_tx_done_event_data_t* edata, void* ctx) {
  (void)node;
  auto* self = static_cast<TwaiNode*>(ctx);
  /* Fires for a failed transmission too -- the driver raises TX_DONE whenever
   * a hardware slot finishes and reports the outcome in is_tx_success, so the
   * slot comes back either way and a bus nobody answers cannot starve the
   * ring. The outcome is not read here because the failure itself already
   * arrives as on_error, which is what error_count_ counts. */
  if (self && edata) self->release_tx_slot(edata->done_tx_frame);
  return false;
}

void TwaiNode::reclaim_tx_slots() {
  portENTER_CRITICAL_SAFE(&tx_mux_);
  const TxSlotRing::ReclaimTicket ticket = tx_ring_.reclaim_begin();
  portEXIT_CRITICAL_SAFE(&tx_mux_);
  if (!ticket.worth_asking) return;

  /* Timeout 0, so this only ever reads state: ESP_OK means idle hardware and
   * an empty pending queue. Bus off answers ESP_ERR_INVALID_STATE, which is
   * deliberately not idle -- the queued frames are restarted on recovery. */
  if (twai_node_transmit_wait_all_done(node_, 0) != ESP_OK) return;

  portENTER_CRITICAL_SAFE(&tx_mux_);
  const size_t freed = tx_ring_.reclaim_commit(ticket);
  portEXIT_CRITICAL_SAFE(&tx_mux_);

  if (freed) {
    /* Warn, not debug: the driver dropped frames it never reported, so a PGN
     * somebody asked for never reached the bus. */
    ESP_LOGW(kTag, "reclaimed %u tx slot(s) the driver abandoned",
             (unsigned)freed);
  }
}

esp_err_t TwaiNode::transmit(const CanFrame& frame, int timeout_ms) {
  if (!node_) return ESP_ERR_INVALID_STATE;
  if (frame.dlc > kCanMaxData) return ESP_ERR_INVALID_ARG;

  /* Into a slot that outlives this call, because the driver reads the frame
   * after it returns -- see TxSlot. */
  portENTER_CRITICAL_SAFE(&tx_mux_);
  const size_t idx = tx_ring_.claim();
  portEXIT_CRITICAL_SAFE(&tx_mux_);
  if (idx == TxSlotRing::kNone) return ESP_ERR_NO_MEM;

  TxSlot* slot = &tx_slots_[idx];
  slot->frame = {};
  slot->frame.header.id = frame.id;
  slot->frame.header.ide = frame.extended;
  slot->frame.header.rtr = frame.remote;
  slot->frame.header.dlc = frame.dlc;
  memcpy(slot->data, frame.data, frame.dlc);
  slot->frame.buffer = slot->data;
  /* dlc and buffer_len both, and equal: _node_queue_tx refuses a frame whose
   * dlc does not match its length. For classic CAN lengths 0..8 they are the
   * same number. */
  slot->frame.buffer_len = frame.dlc;

  const esp_err_t err = twai_node_transmit(node_, &slot->frame, timeout_ms);

  /* A refused frame was never retained: every check in _node_queue_tx returns
   * before it stores the pointer, and the queue path fails on its semaphore --
   * also before the push. So the slot goes straight back; holding it would
   * leak one per refusal and end with a ring that is permanently full. */
  portENTER_CRITICAL_SAFE(&tx_mux_);
  tx_ring_.submitted(idx, err == ESP_OK);
  portEXIT_CRITICAL_SAFE(&tx_mux_);
  return err;
}

/* ---------------------------------------------------------------- ISR side */

bool TwaiNode::on_rx_done(twai_node_handle_t node,
                          const twai_rx_done_event_data_t* edata, void* ctx) {
  (void)edata;
  auto* self = static_cast<TwaiNode*>(ctx);

  // twai_node_receive_from_isr() is only callable here, and only with a
  // buffer of our own to copy the payload into.
  uint8_t data[kCanMaxData];
  twai_frame_t rx = {};
  rx.buffer = data;
  rx.buffer_len = sizeof(data);
  if (twai_node_receive_from_isr(node, &rx) != ESP_OK) return false;

  RxItem item;
  item.msg.frame.id = rx.header.id;
  item.msg.frame.extended = rx.header.ide;
  item.msg.frame.remote = rx.header.rtr;
  item.msg.frame.dlc =
      rx.header.dlc > kCanMaxData ? kCanMaxData : (uint8_t)rx.header.dlc;
  memcpy(item.msg.frame.data, data, item.msg.frame.dlc);
  item.msg.timestamp_us = esp_timer_get_time();

  BaseType_t woken = pdFALSE;
  // A full queue drops the frame, which is what the old driver's rx_queue_len
  // did too. Not logged: this runs in an ISR, and a bus that outruns the
  // consumer would spend all its time logging. Counted instead, so
  // /api/v1/n2k can say "arriving faster than they are consumed" rather than
  // leaving a gap to be guessed at.
  if (xQueueSendFromISR(self->rx_queue_, &item, &woken) == pdTRUE) {
    self->frames_rx_.fetch_add(1, std::memory_order_relaxed);
  } else {
    self->frames_dropped_.fetch_add(1, std::memory_order_relaxed);
  }
  return woken == pdTRUE;
}

/* Bus errors were discarded: .on_error was nullptr, so a device on a
 * mis-terminated or wrong-bitrate bus produced a rising error count that
 * nothing could see. Counting them is what separates "nothing is talking"
 * from "everything is talking and none of it is being understood" -- the
 * two look identical from a candump socket that stays empty.
 *
 * ISR context: two relaxed stores and nothing else. */
bool TwaiNode::on_error(twai_node_handle_t node,
                        const twai_error_event_data_t* edata, void* ctx) {
  (void)node;
  auto* self = static_cast<TwaiNode*>(ctx);
  self->error_count_.fetch_add(1, std::memory_order_relaxed);
  if (edata) {
    self->last_error_flags_.store(edata->err_flags.val,
                                  std::memory_order_relaxed);
  }
  return false;  // no task woken
}

bool TwaiNode::on_state_change(twai_node_handle_t node,
                               const twai_state_change_event_data_t* edata,
                               void* ctx) {
  (void)node;
  auto* self = static_cast<TwaiNode*>(ctx);
  if (edata->new_sta == TWAI_ERROR_BUS_OFF) {
    self->bus_off_count_.fetch_add(1, std::memory_order_relaxed);
    // Recovery is a task's job: twai_node_recover() is not ISR-safe.
    self->recover_pending_.store(true);
  }
  return false;
}

/* --------------------------------------------------------------- task side */

void TwaiNode::rx_task(void* arg) {
  auto* self = static_cast<TwaiNode*>(arg);
  int64_t next_reclaim_us = 0;

  while (self->task_running_.load()) {
    if (self->recover_pending_.exchange(false)) {
      ESP_LOGW(kTag, "bus-off — initiating recovery");
      esp_err_t err = twai_node_recover(self->node_);
      if (err != ESP_OK) {
        ESP_LOGE(kTag, "twai_node_recover failed: %s", esp_err_to_name(err));
      }
    }

    /* Once a second, not once per frame: this loop turns 800+ times a second
     * on a live bus, and the slots the driver forgets are forgotten at bus-off
     * -- a rate nothing needs to be chased at. */
    const int64_t now_us = esp_timer_get_time();
    if (now_us >= next_reclaim_us) {
      next_reclaim_us = now_us + 1000000;
      self->reclaim_tx_slots();
    }

    RxItem item;
    if (xQueueReceive(self->rx_queue_, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
      continue;
    }
    FrameSink sink = self->sink_.load();
    if (sink) sink(self->sink_ctx_.load(), item.msg);
  }

  self->task_ = nullptr;
  vTaskDelete(nullptr);
}

}  // namespace detail
}  // namespace espos_n2k
