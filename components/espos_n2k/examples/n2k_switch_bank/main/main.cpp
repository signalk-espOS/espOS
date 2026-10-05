/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * n2k_switch_bank -- an NMEA 2000 switch bank any MFD on the bus can switch.
 *
 * The device joins the bus as a node of its own (espos_n2k::Node): it claims
 * an address, shows up in the MFD's device list with its product information,
 * reports its eight channels with PGN 127501 (Binary Status Report) and
 * switches them on PGN 127502 (Switch Bank Control) addressed to its bank.
 * The bank instance is the device instance, which the owner sets on the
 * NMEA 2000 settings page or from the MFD.
 *
 * The candump server runs on the same bus, so signalk-server still sees every
 * frame. Its clients may not transmit with the node's own address: that
 * frame would be the node speaking, and nothing the node said.
 *
 * Wiring is the part that goes wrong: a CAN transceiver between the ESP32
 * and the bus, and on a bench 120 Ohm at each end. GET /api/v1/n2k tells a
 * silent bus from a misconfigured one (n2k_candump's README has the table).
 */
#include "driver/gpio.h"
#include "esp_log.h"
#include "espos.h"
#include "espos_n2k/candump_tcp_server.h"
#include "espos_n2k/node.h"
#include "espos_n2k/twai_receiver.h"
#include "espos_n2k/twai_transmitter.h"
#include "espos_n2k_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <atomic>

#include "N2kMessages.h"

namespace {

const char* TAG = "n2k_switch_bank";

/* The board's CAN pins: the Waveshare ESP32-P4 boards. Change them. */
constexpr gpio_num_t kCanTx = GPIO_NUM_20;
constexpr gpio_num_t kCanRx = GPIO_NUM_21;

/* One output per channel, to a relay module or an LED. GPIO_NUM_NC keeps a
 * channel virtual: it switches and reports, and drives nothing. */
constexpr int kChannels = 8;
constexpr gpio_num_t kChannelPins[kChannels] = {
    GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC,
    GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC,
};

/* What an MFD's device list shows. Class 30 / function 140 is "Electrical
 * Distribution / Load Controller" in the NMEA 2000 lists. */
const unsigned long kTransmitPgns[] = {127501, 0};
const unsigned long kReceivePgns[] = {127502, 0};

/* Switch banks commonly repeat 127501 every 2 s besides sending it on every
 * change; an MFD that missed one catches up from the next. */
constexpr TickType_t kStatusPeriod = pdMS_TO_TICKS(2000);

std::atomic<uint8_t> g_state{0};  // bit n = channel n+1
std::atomic<bool> g_changed{true};

void set_channel(int ch, bool on) {
  const uint8_t bit = 1u << ch;
  const uint8_t old = on ? g_state.fetch_or(bit) : g_state.fetch_and(~bit);
  if (((old & bit) != 0) == on) return;
  if (kChannelPins[ch] != GPIO_NUM_NC) gpio_set_level(kChannelPins[ch], on);
  ESP_LOGI(TAG, "channel %d %s", ch + 1, on ? "on" : "off");
  g_changed.store(true);
}

void send_status(espos_n2k::Node& node) {
  tN2kBinaryStatus status;
  N2kResetBinaryStatus(status);
  const uint8_t state = g_state.load();
  for (int ch = 0; ch < kChannels; ch++) {
    N2kSetStatusBinaryOnStatus(
        status, (state >> ch) & 1 ? N2kOnOff_On : N2kOnOff_Off, ch + 1);
  }
  tN2kMsg msg;
  SetN2kBinaryStatus(msg, node.device_instance(), status);
  node.send(msg);
}

}  // namespace

extern "C" void app_main(void) {
  ESP_ERROR_CHECK(espos_start(nullptr));

  for (gpio_num_t pin : kChannelPins) {
    if (pin == GPIO_NUM_NC) continue;
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
  }

  /* static: the node, the server and the receiver's task hold pointers to
   * each other for as long as the device runs. */
  static espos_n2k::TwaiReceiver rx({.tx_pin = kCanTx, .rx_pin = kCanRx});
  static espos_n2k::TwaiTransmitter tx;
  rx.start();
  tx.start();
  espos_n2k_api_register(&rx);

  espos_n2k::NodeConfig cfg;
  cfg.device_class = 30;
  cfg.device_function = 140;
  cfg.model_id = "espOS switch bank";
  cfg.transmit_pgns = kTransmitPgns;
  cfg.receive_pgns = kReceivePgns;
  static espos_n2k::Node node(&rx, &tx, cfg);

  /* Runs on the node task: decode, switch, return. The reply (127501) goes
   * out from the loop below on the change flag. */
  node.add_listener([](const tN2kMsg& msg) {
    if (msg.PGN != 127502) return;
    unsigned char bank;
    tN2kBinaryStatus cmd;
    if (!ParseN2kSwitchbankControl(msg, bank, cmd)) return;
    if (bank != node.device_instance()) return;
    for (int ch = 0; ch < kChannels; ch++) {
      switch (N2kGetStatusOnBinaryStatus(cmd, ch + 1)) {
        case N2kOnOff_On:
          set_channel(ch, true);
          break;
        case N2kOnOff_Off:
          set_channel(ch, false);
          break;
        default:
          break;  // "unavailable": leave this channel alone
      }
    }
  });
  ESP_ERROR_CHECK(node.start());

  static espos_n2k::CandumpTcpServer server(&rx, &tx, {});
  server.set_tx_filter(node.tx_filter());
  server.start();

  TickType_t last = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(20));
    const TickType_t now = xTaskGetTickCount();
    if (node.on_bus() &&
        (g_changed.exchange(false) || now - last >= kStatusPeriod)) {
      send_status(node);
      last = now;
    }
  }
}
