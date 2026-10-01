/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ESPOS_N2K_SRC_TX_SLOT_RING_H_
#define ESPOS_N2K_SRC_TX_SLOT_RING_H_

/// Which of the TWAI node's TX slots the driver is currently holding.
///
/// esp_twai does not copy the frames it is given, so every in-flight frame
/// needs storage that outlives transmit() and a rule for when that storage may
/// be reused (espOS #153, and TwaiNode::TxSlot for why). The storage and the
/// locking live in twai_node.cpp; the *rule* lives here, because the rule is
/// the part that can be got wrong quietly and the part a host test can reach.
/// Nothing in this file includes FreeRTOS or esp_twai.
///
/// The caller does all the locking, and must: release() runs in the TX-done
/// ISR while claim() runs on whichever task called transmit(). twai_node.cpp
/// holds one spinlock across every call below.
///
/// Slots are identified by index. Mapping an index to storage — and a frame
/// the driver handed back to its index — is the caller's job.

#include <stddef.h>
#include <stdint.h>

namespace espos_n2k {
namespace detail {

class TxSlotRing {
 public:
  static constexpr size_t kNone = SIZE_MAX;

  /// What reclaim() observed before the caller asked the driver whether it was
  /// idle. Carried back into reclaim_commit() so it can tell whether the
  /// answer is still true.
  struct ReclaimTicket {
    size_t claims = 0;
    bool worth_asking = false;
  };

  /// `in_use` is storage the caller owns, one bool per slot, and is zeroed
  /// here so a reused ring cannot inherit a previous run's claims.
  void init(bool* in_use, size_t n) {
    in_use_ = in_use;
    n_ = in_use ? n : 0;
    next_ = 0;
    claims_ = 0;
    filling_ = 0;
    for (size_t i = 0; i < n_; i++) in_use_[i] = false;
  }

  size_t size() const { return n_; }

  /// Take the next free slot, or kNone when they are all with the driver.
  ///
  /// Non-blocking by design: a full ring means the bus is slower than the
  /// traffic offered to it, and TwaiTransmitter::set() already counts that
  /// refusal. Round-robin from the last claim rather than always from 0, so
  /// the slots are used evenly instead of hammering the first one.
  ///
  /// The slot counts as "filling" until submitted() says otherwise: a claim
  /// that has not reached the driver yet looks exactly like an idle driver
  /// from the outside, and reclaim must not mistake the two.
  size_t claim() {
    for (size_t i = 0; i < n_; i++) {
      const size_t idx = (next_ + i) % n_;
      if (!in_use_[idx]) {
        in_use_[idx] = true;
        next_ = (idx + 1) % n_;
        claims_++;
        filling_++;
        return idx;
      }
    }
    return kNone;
  }

  /// The claim reached the driver, or did not. `accepted == false` frees the
  /// slot again: a refused frame was never retained, so holding its slot would
  /// leak one per refusal until the ring was permanently full.
  void submitted(size_t idx, bool accepted) {
    if (idx >= n_) return;
    if (filling_ > 0) filling_--;
    if (!accepted) in_use_[idx] = false;
  }

  /// The driver reported it has finished with this slot.
  void release(size_t idx) {
    if (idx < n_) in_use_[idx] = false;
  }

  bool in_use(size_t idx) const { return idx < n_ && in_use_[idx]; }

  size_t in_use_count() const {
    size_t n = 0;
    for (size_t i = 0; i < n_; i++) {
      if (in_use_[i]) n++;
    }
    return n;
  }

  /// Step one of taking back slots the driver abandoned without a callback.
  ///
  /// `worth_asking` is false when there is nothing to reclaim, and also when a
  /// claim is still filling — in that case the driver would answer "idle"
  /// about a frame that is about to be handed to it, and freeing that slot
  /// would put two writers on the same memory.
  ReclaimTicket reclaim_begin() const {
    ReclaimTicket t;
    t.claims = claims_;
    if (filling_ == 0) {
      for (size_t i = 0; i < n_ && !t.worth_asking; i++) {
        t.worth_asking = in_use_[i];
      }
    }
    return t;
  }

  /// Step two: free every held slot, but only if the picture has not moved
  /// since the ticket was taken. A claim made while the caller was asking the
  /// driver moved `claims_`, and taking that slot away is exactly the
  /// corruption the ring exists to prevent. A claim from *before* the ticket
  /// either completed already (its slot is free) or left the driver busy (so
  /// the caller was told "not idle" and never gets here).
  ///
  /// Returns how many slots were taken back, which is how many frames the
  /// driver dropped without reporting them.
  size_t reclaim_commit(const ReclaimTicket& t) {
    if (!t.worth_asking || claims_ != t.claims || filling_ != 0) return 0;
    size_t freed = 0;
    for (size_t i = 0; i < n_; i++) {
      if (in_use_[i]) {
        in_use_[i] = false;
        freed++;
      }
    }
    return freed;
  }

 private:
  bool* in_use_ = nullptr;
  size_t n_ = 0;
  size_t next_ = 0;
  /// Every claim ever made. Only ever compared against itself, so wrapping is
  /// harmless: a wrap would have to land on exactly the sampled value, and the
  /// consequence of that coincidence is one skipped reclaim.
  size_t claims_ = 0;
  /// Claims that have not reached the driver yet.
  size_t filling_ = 0;
};

}  // namespace detail
}  // namespace espos_n2k

#endif  // ESPOS_N2K_SRC_TX_SLOT_RING_H_
