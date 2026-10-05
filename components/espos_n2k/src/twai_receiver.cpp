/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#include "espos_n2k/twai_receiver.h"

#include <mutex>

#include "esp_log.h"
#include "esp_timer.h"

#include "listener_table.h"
#include "twai_node.h"

namespace espos_n2k {

namespace {
constexpr const char* kTag = "twai_rx";
}

// Dispatch holds the lock for the whole pass, which is what lets
// remove_listener() promise the callback is no longer running when it
// returns. std::mutex rather than a FreeRTOS mutex: a receiver is often a
// file-scope static, constructed before app_main().
struct TwaiReceiver::Listeners {
  std::mutex lock;
  detail::ListenerTable<FrameFn, CONFIG_ESPOS_N2K_MAX_LISTENERS> table;
};

TwaiReceiver::TwaiReceiver(const TwaiReceiverConfig& config)
    : config_(config), listeners_(new Listeners) {}

TwaiReceiver::~TwaiReceiver() {
  stop();
  delete listeners_;
}

TwaiReceiver::ListenerId TwaiReceiver::add_listener(FrameFn fn) {
  std::lock_guard<std::mutex> g(listeners_->lock);
  const ListenerId id = listeners_->table.add(std::move(fn));
  if (id == kNoListener) {
    ESP_LOGE(kTag,
             "no free frame listener slot (CONFIG_ESPOS_N2K_MAX_LISTENERS=%d)",
             CONFIG_ESPOS_N2K_MAX_LISTENERS);
  }
  return id;
}

void TwaiReceiver::remove_listener(ListenerId id) {
  std::lock_guard<std::mutex> g(listeners_->lock);
  listeners_->table.remove(id);
  if (id == on_frame_id_) on_frame_id_ = kNoListener;
}

void TwaiReceiver::loopback(const CanMessage& msg, ListenerId skip) {
  std::lock_guard<std::mutex> g(listeners_->lock);
  listeners_->table.dispatch_except(skip, msg);
}

void TwaiReceiver::set_on_frame(FrameFn fn) {
  std::lock_guard<std::mutex> g(listeners_->lock);
  listeners_->table.remove(on_frame_id_);
  on_frame_id_ = fn ? listeners_->table.add(std::move(fn)) : kNoListener;
}

void TwaiReceiver::start() {
  if (running_.exchange(true)) return;

  detail::TwaiNodeConfig cfg;
  cfg.tx_pin = config_.tx_pin;
  cfg.rx_pin = config_.rx_pin;
  cfg.bitrate = config_.bitrate;
  cfg.rx_queue_depth = config_.rx_queue_depth;

  // The node logs why on every failure path (unset pins, no free controller);
  // repeating it here would only say it twice.
  if (detail::TwaiNode::instance().acquire(cfg) != ESP_OK) {
    running_.store(false);
    return;
  }
  detail::TwaiNode::instance().set_sink(&TwaiReceiver::sink, this);
}

void TwaiReceiver::stop() {
  if (!running_.exchange(false)) return;
  // Unhook first: releasing may keep the bus up for a transmitter that is
  // still running, and a frame arriving after this object is gone would call
  // into a destroyed std::function.
  detail::TwaiNode::instance().set_sink(nullptr, nullptr);
  detail::TwaiNode::instance().release();
}

bool TwaiReceiver::bus_running() const {
  return detail::TwaiNode::instance().running();
}

uint32_t TwaiReceiver::frames_received() const {
  return detail::TwaiNode::instance().frames_received();
}

uint32_t TwaiReceiver::frames_dropped() const {
  return detail::TwaiNode::instance().frames_dropped();
}

uint32_t TwaiReceiver::error_count() const {
  return detail::TwaiNode::instance().error_count();
}

uint32_t TwaiReceiver::last_error_flags() const {
  return detail::TwaiNode::instance().last_error_flags();
}

uint32_t TwaiReceiver::bus_off_count() const {
  return detail::TwaiNode::instance().bus_off_count();
}

/// Runs on the node's task, one frame at a time — the same contract the
/// old twai_receive() loop offered, so callbacks written against it are
/// unaffected by the driver change.
void TwaiReceiver::sink(void* ctx, const CanMessage& msg) {
  auto* self = static_cast<TwaiReceiver*>(ctx);
  self->last_rx_us_.store(msg.timestamp_us, std::memory_order_relaxed);
  std::lock_guard<std::mutex> g(self->listeners_->lock);
  self->listeners_->table.dispatch(msg);
}

}  // namespace espos_n2k
