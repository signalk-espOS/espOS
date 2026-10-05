/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#include "espos_n2k/node.h"

#include <atomic>
#include <cstring>
#include <mutex>

#include "espos_config.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "NMEA2000.h"
#include "listener_table.h"
#include "node_identity.h"

namespace espos_n2k {

namespace {

constexpr const char* kTag = "n2k_node";
constexpr const char* kNs = "n2k";
constexpr const char* kNvsNs = "espos_n2k";
constexpr const char* kNvsAddress = "addr";
constexpr TickType_t kLoopTicks = pdMS_TO_TICKS(10);
// The library retries Open() once a second on its own; this only decides
// when the log says so, once.
constexpr TickType_t kOpenWarnTicks = pdMS_TO_TICKS(5000);

// tNMEA2000 over espos_n2k. The library calls these from ParseMessages() and
// SendMsg(), both only ever on the node task.
class Bus : public tNMEA2000 {
 public:
  Bus(TwaiReceiver* rx, TwaiTransmitter* tx) : rx_(rx), tx_(tx) {}

  void set_frames(QueueHandle_t q, TwaiReceiver::ListenerId own) {
    frames_ = q;
    own_ = own;
  }

  // Address claim in progress: the library refuses every other message until
  // it settles, so queued sends wait for it.
  bool claiming() { return IsAddressClaimStarted(0); }

 protected:
  // The receiver owns the bus and its pins; false until it is up, and the
  // library tries again a second later.
  bool CANOpen() override { return rx_->bus_running(); }

  bool CANSendFrame(unsigned long id, unsigned char len,
                    const unsigned char* buf, bool) override {
    CanMessage m;
    m.frame.id = id;
    m.frame.extended = true;
    m.frame.dlc = len > kCanMaxData ? kCanMaxData : len;
    memcpy(m.frame.data, buf, m.frame.dlc);
    // False keeps the frame in the library's own send buffer, retried from
    // ParseMessages(), rather than losing it and with it a whole
    // fast-packet message.
    if (!tx_->transmit(m)) return false;
    // So the other listeners -- a candump server above all -- see the
    // node's frames as every other device on the bus does.
    m.timestamp_us = esp_timer_get_time();
    rx_->loopback(m, own_);
    return true;
  }

  bool CANGetFrame(unsigned long& id, unsigned char& len,
                   unsigned char* buf) override {
    CanFrame f;
    if (!frames_ || xQueueReceive(frames_, &f, 0) != pdTRUE) return false;
    id = f.id;
    len = f.dlc;
    memcpy(buf, f.data, f.dlc);
    return true;
  }

 private:
  TwaiReceiver* rx_;
  TwaiTransmitter* tx_;
  QueueHandle_t frames_ = nullptr;
  TwaiReceiver::ListenerId own_ = TwaiReceiver::kNoListener;
};

}  // namespace

class Node::Impl : public tNMEA2000::tMsgHandler {
 public:
  Impl(TwaiReceiver* rx, TwaiTransmitter* tx, const NodeConfig& config)
      : tMsgHandler(0), rx(rx), config(config), bus(rx, tx) {}

  // PGN 0: every message the library hands on, already assembled.
  void HandleMsg(const tN2kMsg& msg) override {
    std::lock_guard<std::mutex> g(listeners_lock);
    listeners.dispatch(msg);
  }

  static void task(void* arg);
  static void on_config(const char* ns, const char* key, void* arg);
  void loop();
  void apply_settings();
  void store_instances();
  void publish_state();

  TwaiReceiver* rx;
  NodeConfig config;
  Bus bus;
  char serial[13] = {};

  QueueHandle_t frames = nullptr;
  QueueHandle_t outbox = nullptr;
  TwaiReceiver::ListenerId rx_listener = TwaiReceiver::kNoListener;
  nvs_handle_t nvs = 0;

  std::mutex listeners_lock;
  detail::ListenerTable<MsgFn, CONFIG_ESPOS_N2K_MAX_LISTENERS> listeners;

  std::atomic<bool> started{false};
  std::atomic<bool> settings_changed{false};
  std::atomic<bool> on_bus{false};
  std::atomic<uint8_t> address{detail::kNullAddress};
  std::atomic<uint64_t> name{0};
  std::atomic<uint8_t> device_instance{0};
  std::atomic<uint8_t> system_instance{0};
  std::atomic<uint32_t> frames_dropped{0};
};

void Node::Impl::task(void* arg) { static_cast<Impl*>(arg)->loop(); }

// On the writer's task: only note it, the node task applies it.
void Node::Impl::on_config(const char* ns, const char*, void* arg) {
  if (strcmp(ns, kNs) == 0) {
    static_cast<Impl*>(arg)->settings_changed.store(true);
  }
}

// Settings -> NAME. A NAME that changed is announced with a fresh address
// claim, which is how every other node learns the new instance.
void Node::Impl::apply_settings() {
  int32_t di = 0, si = 0;
  espos_config_get_i32(kNs, "device_instance", &di);
  espos_config_get_i32(kNs, "system_instance", &si);
  const auto inst = static_cast<uint8_t>(di);
  bus.SetDeviceInformationInstances(detail::device_instance_lower(inst),
                                    detail::device_instance_upper(inst),
                                    static_cast<uint8_t>(si));
}

// NAME -> settings: an MFD that set the instance through a group function
// changed the library's copy, and the next boot must come up with it.
void Node::Impl::store_instances() {
  const tNMEA2000::tDeviceInformation di = bus.GetDeviceInformation();
  int32_t cur = -1;
  if (espos_config_get_i32(kNs, "device_instance", &cur) != ESP_OK ||
      cur != di.GetDeviceInstance()) {
    espos_config_set_i32(kNs, "device_instance", di.GetDeviceInstance());
  }
  if (espos_config_get_i32(kNs, "system_instance", &cur) != ESP_OK ||
      cur != di.GetSystemInstance()) {
    espos_config_set_i32(kNs, "system_instance", di.GetSystemInstance());
  }
}

void Node::Impl::publish_state() {
  const tNMEA2000::tDeviceInformation di = bus.GetDeviceInformation();
  name.store(di.GetName());
  device_instance.store(di.GetDeviceInstance());
  system_instance.store(di.GetSystemInstance());
}

void Node::Impl::loop() {
  const TickType_t began = xTaskGetTickCount();
  bool opened = false, warned = false;
  bool was_on_bus = false;
  tN2kMsg pending;
  bool have_pending = false;

  for (;;) {
    // Also opens the bus: until it is open, ParseMessages() retries Open().
    bus.ParseMessages();
    if (!bus.IsOpen()) {
      if (!warned && xTaskGetTickCount() - began > kOpenWarnTicks) {
        warned = true;
        ESP_LOGE(kTag,
                 "the CAN bus is not up -- start the receiver before the "
                 "node; still retrying");
      }
      vTaskDelay(kLoopTicks);
      continue;
    }
    if (!opened) {
      opened = true;
      ESP_LOGI(kTag, "open; claiming address %u", (unsigned)bus.GetN2kSource());
    }

    if (settings_changed.exchange(false)) apply_settings();
    if (bus.ReadResetDeviceInformationChanged()) {
      publish_state();
      store_instances();
      bus.SendIsoAddressClaim();
      ESP_LOGI(kTag, "device instance %u, system instance %u",
               (unsigned)device_instance.load(),
               (unsigned)system_instance.load());
    }

    const uint8_t src = bus.GetN2kSource();
    address.store(src);
    if (bus.ReadResetAddressChanged()) {
      // Back on the same address next boot, as the standard expects.
      nvs_set_u8(nvs, kNvsAddress, src);
      nvs_commit(nvs);
    }
    const bool now_on_bus = src <= N2kMaxCanBusAddress && !bus.claiming();
    on_bus.store(now_on_bus);
    if (now_on_bus != was_on_bus) {
      was_on_bus = now_on_bus;
      if (now_on_bus) {
        ESP_LOGI(kTag, "on the bus at address %u", (unsigned)src);
      } else {
        ESP_LOGW(kTag, "lost the address; claiming another");
      }
    }

    // Drained only while the address is held: the library refuses messages
    // during a claim, and a refused one would be gone.
    while (now_on_bus) {
      if (!have_pending && xQueueReceive(outbox, &pending, 0) != pdTRUE) {
        break;
      }
      have_pending = true;
      if (!bus.SendMsg(pending)) {
        // Either the transmit path is full (the library buffers what it
        // can) or the message is invalid; neither improves by retrying in a
        // loop, and an invalid one must not block the queue for ever.
        if (!bus.claiming()) {
          ESP_LOGW(kTag, "PGN %lu not sent", (unsigned long)pending.PGN);
          have_pending = false;
        }
        break;
      }
      have_pending = false;
    }

    vTaskDelay(kLoopTicks);
  }
}

Node::Node(TwaiReceiver* receiver, TwaiTransmitter* transmitter,
           const NodeConfig& config)
    : impl_(new Impl(receiver, transmitter, config)) {}

// Never destroyed while running (node.h); the task holds impl_.
Node::~Node() {
  if (!impl_->started.load()) delete impl_;
}

esp_err_t Node::start() {
  Impl& s = *impl_;
  const NodeConfig& c = s.config;
  if (!c.model_id || !c.device_class || !c.device_function) {
    ESP_LOGE(kTag,
             "NodeConfig needs model_id, device_class and device_function");
    return ESP_ERR_INVALID_ARG;
  }
  if (s.started.exchange(true)) return ESP_ERR_INVALID_STATE;

  esp_err_t err = nvs_open(kNvsNs, NVS_READWRITE, &s.nvs);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs_open: %s", esp_err_to_name(err));
    s.started.store(false);
    return err;
  }
  uint8_t address = c.preferred_address;
  nvs_get_u8(s.nvs, kNvsAddress, &address);

  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_BASE);
  detail::serial_from_mac(mac, s.serial, sizeof(s.serial));
  const uint32_t unique = c.unique_number
                              ? (c.unique_number & detail::kUniqueNumberMask)
                              : detail::unique_number_from_mac(mac);

  s.frames = xQueueCreate(CONFIG_ESPOS_N2K_RX_QUEUE_DEPTH, sizeof(CanFrame));
  s.outbox = xQueueCreate(CONFIG_ESPOS_N2K_NODE_TX_QUEUE, sizeof(tN2kMsg));
  if (!s.frames || !s.outbox) {
    s.started.store(false);
    return ESP_ERR_NO_MEM;
  }

  s.bus.SetProductInformation(
      s.serial, c.product_code, c.model_id, esp_app_get_description()->version,
      c.model_version ? c.model_version : "", c.load_equivalency);
  s.bus.SetDeviceInformation(unique, c.device_function, c.device_class,
                             c.manufacturer_code, c.industry_group);
  s.apply_settings();
  s.bus.ReadResetDeviceInformationChanged();  // the boot value, not a change
  s.publish_state();
  s.bus.SetMode(tNMEA2000::N2km_NodeOnly, address);
  s.bus.EnableForward(false);
  if (c.transmit_pgns) s.bus.ExtendTransmitMessages(c.transmit_pgns);
  if (c.receive_pgns) s.bus.ExtendReceiveMessages(c.receive_pgns);
  s.bus.AttachMsgHandler(&s);
  s.address.store(address);

  // Copy and return: this runs on the receiver's task, which every other
  // listener on the bus is waiting behind.
  s.rx_listener = s.rx->add_listener([&s](const CanMessage& m) {
    if (xQueueSend(s.frames, &m.frame, 0) != pdTRUE) {
      s.frames_dropped.fetch_add(1, std::memory_order_relaxed);
    }
  });
  if (s.rx_listener == TwaiReceiver::kNoListener) {
    s.started.store(false);
    return ESP_ERR_NO_MEM;
  }
  s.bus.set_frames(s.frames, s.rx_listener);
  espos_config_subscribe(&Impl::on_config, &s);

  // Not Open() here: the library opens only once a millisecond has passed
  // since it was made, so an Open() this early returns false without trying.
  // The task's ParseMessages() opens it and keeps retrying.
  if (xTaskCreate(&Impl::task, "n2k_node", CONFIG_ESPOS_N2K_NODE_TASK_STACK, &s,
                  4, nullptr) != pdPASS) {
    s.rx->remove_listener(s.rx_listener);
    espos_config_unsubscribe(&Impl::on_config, &s);
    s.started.store(false);
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(kTag, "%s: class %u function %u, unique number %lu, serial %s",
           c.model_id, (unsigned)c.device_class, (unsigned)c.device_function,
           (unsigned long)unique, s.serial);
  return ESP_OK;
}

bool Node::send(const tN2kMsg& msg) {
  if (!impl_->started.load() || !impl_->outbox) return false;
  return xQueueSend(impl_->outbox, &msg, 0) == pdTRUE;
}

Node::ListenerId Node::add_listener(MsgFn fn) {
  std::lock_guard<std::mutex> g(impl_->listeners_lock);
  return impl_->listeners.add(std::move(fn));
}

void Node::remove_listener(ListenerId id) {
  std::lock_guard<std::mutex> g(impl_->listeners_lock);
  impl_->listeners.remove(id);
}

bool Node::on_bus() const { return impl_->on_bus.load(); }
uint8_t Node::address() const {
  return on_bus() ? impl_->address.load() : detail::kNullAddress;
}
uint64_t Node::name() const { return impl_->name.load(); }
uint8_t Node::device_instance() const { return impl_->device_instance.load(); }
uint8_t Node::system_instance() const { return impl_->system_instance.load(); }

std::function<bool(const CanFrame&)> Node::tx_filter() const {
  Impl* s = impl_;
  // The address the node holds or is claiming: a client frame with it during
  // the claim would contest the node's own claim.
  return [s](const CanFrame& f) {
    return !s->started.load() ||
           detail::foreign_frame_allowed(f, s->address.load());
  };
}

tNMEA2000& Node::library() { return impl_->bus; }

}  // namespace espos_n2k
