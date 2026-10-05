/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ESPOS_N2K_NODE_H_
#define ESPOS_N2K_NODE_H_

#include <cstdint>
#include <functional>

#include "esp_err.h"

#include "espos_n2k/can_frame.h"
#include "espos_n2k/twai_receiver.h"
#include "espos_n2k/twai_transmitter.h"

#include "N2kMsg.h"

class tNMEA2000;

namespace espos_n2k {

/// What the device is on the bus. Everything here is fixed for the life of
/// the firmware; the instances, which the owner sets per boat, are settings
/// (`n2k.device_instance`, `n2k.system_instance`), not fields.
struct NodeConfig {
  /// NAME: device class and function from the NMEA 2000 lists (canboat's
  /// docs carry them: class 30 / function 140 is "Electrical Distribution /
  /// Load Controller", class 25 / function 130 "Network Device / Gateway").
  /// Required: 0 is "reserved", which an MFD shows as an unknown device.
  uint8_t device_class = 0;
  uint8_t device_function = 0;
  /// 2046 is the code the NMEA2000 library's examples use for devices that
  /// are not certified; no manufacturer holds it.
  uint16_t manufacturer_code = 2046;
  /// 4 is Marine.
  uint8_t industry_group = 4;
  /// 21 bits. 0 derives it from the base MAC, which is what keeps two boards
  /// running the same firmware from having the same NAME.
  uint32_t unique_number = 0;

  /// Product information (PGN 126996), what an MFD's device list shows.
  /// model_id is required (up to 32 characters); the software version is the
  /// app's own (esp_app_desc_t::version) and the serial code the base MAC.
  const char* model_id = nullptr;
  const char* model_version = "";  ///< the hardware revision
  uint16_t product_code = 0;
  /// Load equivalency number: current drawn from the bus, in 50 mA units.
  uint8_t load_equivalency = 1;

  /// Source address to claim first on a board that has never claimed one;
  /// after that the address it last held is stored and claimed again, as the
  /// standard expects. Another node with a higher-priority NAME wins it, and
  /// the library moves on to a free one.
  uint8_t preferred_address = 34;

  /// The PGNs this application sends and receives, for the lists the node
  /// answers with (PGN 126464). Zero-terminated, in static storage: the
  /// library keeps the pointers. The PGNs the node itself handles (address
  /// claim, ISO request, product information, heartbeat ...) are listed
  /// already.
  const unsigned long* transmit_pgns = nullptr;
  const unsigned long* receive_pgns = nullptr;
};

/// The device as an NMEA 2000 node in its own right: a NAME, the ISO address
/// claim and its contest, answers to ISO requests for the address claim,
/// product and configuration information and the PGN lists, a heartbeat, and
/// the group functions an MFD uses to set the device and system instance.
/// The NMEA2000 library (vendored) does the protocol; this class gives it the
/// receiver and transmitter, its own task, the settings and the stored
/// address.
///
/// Build with CONFIG_ESPOS_N2K_NODE. One Node per bus, made once and never
/// destroyed -- a `static`, like the receiver it reads from.
///
/// It shares the bus with anything else on the receiver: a candump server on
/// the same receiver keeps working, and set_tx_filter(node.tx_filter()) on
/// that server stops its clients transmitting as the node (docs/n2k.md,
/// "Node and bridge on one bus").
class Node {
 public:
  using MsgFn = std::function<void(const tN2kMsg&)>;
  using ListenerId = uint32_t;
  static constexpr ListenerId kNoListener = 0;

  /// Both must outlive the node. Start them before start().
  Node(TwaiReceiver* receiver, TwaiTransmitter* transmitter,
       const NodeConfig& config);
  ~Node();
  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;

  /// Reads the settings and the stored address, then starts the node task,
  /// which opens the library and claims an address. Call after espos_start()
  /// (the settings store and NVS must be up) and after the receiver and
  /// transmitter are started. ESP_ERR_INVALID_ARG for a config without
  /// model_id, device class or function; ESP_ERR_INVALID_STATE when already
  /// started.
  esp_err_t start();

  /// Queues a message for the node task to send with the node's source
  /// address, from any task, without blocking. The PGN, priority,
  /// destination and data come from `msg`; the source is filled in. False
  /// when the node is not started or CONFIG_ESPOS_N2K_NODE_TX_QUEUE is full.
  /// A message queued before the address claim settles goes out after it.
  bool send(const tN2kMsg& msg);

  /// Adds a listener for every message the node receives -- assembled, so a
  /// fast-packet PGN arrives once, whole. Runs on the node task: return
  /// quickly, and send() from it rather than blocking. Do not add or remove
  /// listeners from inside one. kNoListener when all slots are taken.
  ListenerId add_listener(MsgFn fn);
  void remove_listener(ListenerId id);

  /// True once an address is claimed and held.
  bool on_bus() const;
  /// The claimed source address; 254 while there is none.
  uint8_t address() const;
  /// The 64-bit NAME, as sent in the address claim.
  uint64_t name() const;
  uint8_t device_instance() const;
  uint8_t system_instance() const;

  /// For CandumpTcpServer::set_tx_filter(): refuses client frames that carry
  /// the node's own source address.
  std::function<bool(const CanFrame&)> tx_filter() const;

  /// The library itself, for what this class does not wrap (configuration
  /// information, installation descriptions, extra fast-packet PGNs). Not
  /// thread-safe: use it before start(), or from a listener, which runs on
  /// the node task.
  tNMEA2000& library();

 private:
  class Impl;
  Impl* impl_;
};

}  // namespace espos_n2k

#endif  // ESPOS_N2K_NODE_H_
