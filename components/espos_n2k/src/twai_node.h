/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ESPOS_N2K_SRC_TWAI_NODE_H_
#define ESPOS_N2K_SRC_TWAI_NODE_H_

/// The one TWAI node, shared by the receiver and the transmitter.
///
/// IDF 6's esp_twai API allocates a *node* and hands back a handle, where the
/// old driver/twai.h API installed a process-wide singleton that any caller
/// could reach through twai_receive()/twai_transmit(). TwaiReceiver and
/// TwaiTransmitter were written against that singleton and are separate
/// objects with separate lifetimes, so somebody has to own the handle now.
/// This is that somebody: a reference-counted holder that behaves the way the
/// old global driver did — the first user to start configures the bus, the
/// last one to stop tears it down.
///
/// Internal to the component. Nothing here is part of the public API.

#include <atomic>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "espos_n2k/can_frame.h"
#include "tx_slot_ring.h"

namespace espos_n2k {
namespace detail {

struct TwaiNodeConfig {
  gpio_num_t tx_pin = GPIO_NUM_NC;
  gpio_num_t rx_pin = GPIO_NUM_NC;
  uint32_t bitrate = 250000;
  size_t rx_queue_depth = CONFIG_ESPOS_N2K_RX_QUEUE_DEPTH;
  size_t tx_queue_depth = 32;
};

class TwaiNode {
 public:
  using FrameSink = void (*)(void* ctx, const CanMessage& msg);

  static TwaiNode& instance();

  /// Start (or join) the bus. The first caller's config wins; a later caller
  /// with a different one gets a warning, not a silently reconfigured bus.
  /// Reference-counted against release().
  esp_err_t acquire(const TwaiNodeConfig& config);
  void release();

  bool running() const { return refs_.load() > 0; }

  /// Frames are delivered on the node's own task, never from the ISR.
  void set_sink(FrameSink sink, void* ctx);

  /// Copy `frame` into a slot of our own and hand it to the driver.
  ///
  /// `timeout_ms` is the driver's, for waiting on its TX queue. Running out of
  /// *slots* does not wait: the ring is deliberately larger than everything
  /// the driver can hold (see acquire()), so a refusal here means the driver
  /// was at its own ceiling too, and returning ESP_ERR_NO_MEM at once is what
  /// the one caller wants -- TwaiTransmitter::set() passes 0 and counts the
  /// drop. Waiting would also cost this function its ISR-callability, which
  /// the spinlock and esp_twai's own _ISR-flavoured checks otherwise give it.
  esp_err_t transmit(const CanFrame& frame, int timeout_ms);

  uint32_t bus_off_count() const { return bus_off_count_.load(); }

  /* Counters for the status endpoint. A bus that is wired but silent, one
   * that is not wired at all, and one whose driver never started all look
   * identical from the network without these -- which is exactly the
   * position the panel was in when its N2K bus went quiet and there was no
   * USB cable to ask. */
  uint32_t frames_received() const { return frames_rx_.load(); }
  uint32_t frames_dropped() const { return frames_dropped_.load(); }
  uint32_t error_count() const { return error_count_.load(); }
  uint32_t last_error_flags() const { return last_error_flags_.load(); }

 private:
  TwaiNode() = default;

  /// A frame handed to the driver, and the payload it points at.
  ///
  /// esp_twai does not copy: twai_node_transmit() stores the POINTER (queued
  /// frames go into twai_frame_queue as `.data = data`, and the immediate path
  /// keeps it in p_curr_tx), and the TX-done ISR reads the header and the
  /// buffer from it later. A frame built on transmit()'s stack therefore dies
  /// while the driver still means to read it -- which crashed a board in
  /// twai_ll_format_frame_buffer, and when it did not crash sent whatever had
  /// since overwritten that stack as a PGN (espOS #153).
  ///
  /// So every in-flight frame gets a slot that lives as long as the node does.
  /// The driver tells us which one it finished with, so a slot is held from
  /// transmit() until on_tx_done and no longer.
  struct TxSlot {
    twai_frame_t frame;
    uint8_t data[kCanMaxData];
  };

  /// Which slot the driver handed back, or TxSlotRing::kNone for a frame that
  /// is not one of ours. Caller holds tx_mux_.
  size_t tx_slot_index(const twai_frame_t* frame) const;
  /// Called from the TX-done ISR.
  void release_tx_slot(const twai_frame_t* frame);
  /// Take back slots the driver abandoned without telling us.
  ///
  /// A bus-off does not report the frames it drops. When the bus comes back,
  /// esp_twai_onchip.c's state-change ISR refills p_curr_tx[] from the pending
  /// queue -- or memsets it when that queue is empty -- and never calls
  /// on_tx_done for whatever was in a hardware slot at the time. Up to
  /// TWAI_HAL_TX_BUFFER_SLOT_NUM (8) frames per bus-off are therefore never
  /// acknowledged, and without this their slots stay claimed for good: a few
  /// bus-offs on a marginal bus and the ring is full with the bus healthy
  /// again, transmit() refusing everything for ever.
  ///
  /// twai_node_transmit_wait_all_done(node, 0) answers the one question that
  /// settles it: ESP_OK means the hardware is idle AND the pending queue is
  /// empty, so the driver holds no frame pointer at all and every claimed slot
  /// is free by definition. Bus-off reports ESP_ERR_INVALID_STATE, which is
  /// deliberately not idle -- those queued frames are restarted on recovery,
  /// so the driver is still reading them.
  void reclaim_tx_slots();

  static bool on_tx_done(twai_node_handle_t node,
                         const twai_tx_done_event_data_t* edata, void* ctx);
  static bool on_rx_done(twai_node_handle_t node,
                         const twai_rx_done_event_data_t* edata, void* ctx);
  static bool on_state_change(twai_node_handle_t node,
                              const twai_state_change_event_data_t* edata,
                              void* ctx);
  static bool on_error(twai_node_handle_t node,
                       const twai_error_event_data_t* edata, void* ctx);
  static void rx_task(void* arg);

  void teardown();

  twai_node_handle_t node_ = nullptr;
  TwaiNodeConfig config_;
  std::atomic<int> refs_{0};
  std::atomic<bool> task_running_{false};
  /// Set from the state-change ISR, acted on by the task: twai_node_recover()
  /// is not safe to call from an ISR.
  std::atomic<bool> recover_pending_{false};
  std::atomic<uint32_t> bus_off_count_{0};
  std::atomic<uint32_t> frames_rx_{0};
  std::atomic<uint32_t> frames_dropped_{0};
  std::atomic<uint32_t> error_count_{0};
  std::atomic<uint32_t> last_error_flags_{0};

  /// Freed only after twai_node_delete(), so the driver cannot be holding a
  /// pointer into it. tx_in_use_ is the ring's bookkeeping storage, kept
  /// beside the frames rather than inside them so the ring itself stays free
  /// of esp_twai and can be host-tested.
  TxSlot* tx_slots_ = nullptr;
  bool* tx_in_use_ = nullptr;
  TxSlotRing tx_ring_;
  /// A spinlock, not a mutex: release runs in the TX-done ISR. Held across
  /// every tx_ring_ call and nothing else.
  portMUX_TYPE tx_mux_ = portMUX_INITIALIZER_UNLOCKED;

  QueueHandle_t rx_queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t lock_ = nullptr;

  /// Read by the task, written by set_sink(); a pointer pair small enough
  /// that a torn read is not possible on any target espOS builds for.
  std::atomic<FrameSink> sink_{nullptr};
  std::atomic<void*> sink_ctx_{nullptr};
};

}  // namespace detail
}  // namespace espos_n2k

#endif  // ESPOS_N2K_SRC_TWAI_NODE_H_
