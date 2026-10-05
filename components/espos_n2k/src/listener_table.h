/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ESPOS_N2K_SRC_LISTENER_TABLE_H_
#define ESPOS_N2K_SRC_LISTENER_TABLE_H_

#include <cstddef>
#include <cstdint>
#include <utility>

namespace espos_n2k {
namespace detail {

/// Fixed-size table of callbacks, addressed by handle rather than by slot.
///
/// Header-only and IDF-free so the bookkeeping is host-tested; the caller
/// supplies the locking. Handles are never reused within a run (a 32-bit
/// counter, skipping 0), so a stale handle removed twice cannot take out the
/// listener that has since moved into its slot -- which a slot index would.
template <typename Fn, size_t N>
class ListenerTable {
 public:
  using Handle = uint32_t;
  static constexpr Handle kNone = 0;

  /// The new listener's handle, or kNone when the table is full or `fn` is
  /// empty.
  Handle add(Fn fn) {
    if (!fn) return kNone;
    for (auto& s : slots_) {
      if (s.handle != kNone) continue;
      if (++next_ == kNone) ++next_;
      s.handle = next_;
      s.fn = std::move(fn);
      return s.handle;
    }
    return kNone;
  }

  /// False when no listener has that handle (already removed, or never
  /// added).
  bool remove(Handle h) {
    if (h == kNone) return false;
    for (auto& s : slots_) {
      if (s.handle != h) continue;
      s.handle = kNone;
      s.fn = Fn();
      return true;
    }
    return false;
  }

  /// Calls every listener in slot order.
  template <typename... Args>
  void dispatch(Args&&... args) const {
    for (const auto& s : slots_) {
      if (s.handle != kNone) s.fn(args...);
    }
  }

  /// dispatch(), skipping the listener with handle `skip`.
  template <typename... Args>
  void dispatch_except(Handle skip, Args&&... args) const {
    for (const auto& s : slots_) {
      if (s.handle != kNone && s.handle != skip) s.fn(args...);
    }
  }

  size_t size() const {
    size_t n = 0;
    for (const auto& s : slots_) n += s.handle != kNone;
    return n;
  }

  static constexpr size_t capacity() { return N; }

 private:
  struct Slot {
    Handle handle = kNone;
    Fn fn;
  };
  Slot slots_[N];
  Handle next_ = kNone;
};

}  // namespace detail
}  // namespace espos_n2k

#endif  // ESPOS_N2K_SRC_LISTENER_TABLE_H_
