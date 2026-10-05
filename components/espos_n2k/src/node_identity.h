/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ESPOS_N2K_SRC_NODE_IDENTITY_H_
#define ESPOS_N2K_SRC_NODE_IDENTITY_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "espos_n2k/can_frame.h"

/// The arithmetic around espos_n2k::Node that does not need the bus or the
/// library: header-only and IDF-free, so it is host-tested. Each one is a
/// value that ends up on the wire, where a wrong bit is a device that claims
/// someone else's identity or answers as a different instance.
namespace espos_n2k {
namespace detail {

/// The NAME's unique number is 21 bits.
inline constexpr uint32_t kUniqueNumberMask = 0x1FFFFF;

/// Source addresses 254 ("cannot claim") and 255 (global) are never a
/// node's own.
inline constexpr uint8_t kNullAddress = 254;

/// Unique number from the base MAC's last 21 bits. The MAC's top three bytes
/// are the vendor's OUI, the same on every ESP32, so the low bits are the
/// ones that differ between two boards.
inline uint32_t unique_number_from_mac(const uint8_t mac[6]) {
  return ((static_cast<uint32_t>(mac[3]) << 16) |
          (static_cast<uint32_t>(mac[4]) << 8) | mac[5]) &
         kUniqueNumberMask;
}

/// The product information's serial code: the base MAC as 12 hex digits,
/// the number printed on the module's label.
inline void serial_from_mac(const uint8_t mac[6], char* out, size_t size) {
  snprintf(out, size, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
}

/// NMEA 2000's 8-bit device instance is two NAME fields: the lower three
/// bits and the upper five.
inline uint8_t device_instance_lower(uint8_t instance) {
  return instance & 0x07;
}
inline uint8_t device_instance_upper(uint8_t instance) { return instance >> 3; }

/// Source address of an NMEA 2000 frame: the low byte of the 29-bit ID.
inline uint8_t source_address(const CanFrame& f) { return f.id & 0xFF; }

/// Whether a frame from another sender (a candump client) may go on the bus
/// while the node holds `own`. Refused: an extended frame carrying the
/// node's own source address. On the wire it would be the node's frame, so a
/// peer would read it as the node speaking -- or, for an address claim, as
/// the node changing its NAME. Allowed: everything else, including any
/// address while the node holds none.
inline bool foreign_frame_allowed(const CanFrame& f, uint8_t own) {
  if (!f.extended || own >= kNullAddress) return true;
  return source_address(f) != own;
}

}  // namespace detail
}  // namespace espos_n2k

#endif  // ESPOS_N2K_SRC_NODE_IDENTITY_H_
