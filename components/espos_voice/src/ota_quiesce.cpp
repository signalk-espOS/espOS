// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
//
// Park the wake pipeline while an OTA image downloads.
//
// esp-sr's AFE is a hard real-time consumer: on the P4 cockpit panel it feeds
// WakeNet at 16 kHz from two microphones through SE(BSS). Draining a
// multi-megabyte image saturates the core the network stack runs on, the AFE
// stops being serviced ("Ringbuffer of AFE(FEED) is full"), the idle task is
// starved, and IDF's task watchdog aborts the firmware mid-download. Measured
// on a Waveshare ESP32-P4-WIFI6-Touch-LCD-7B: 100% reproducible on a 4.4 MB
// image, over both HTTPS and plain HTTP.
//
// These are the strong definitions of the weak hooks in espos_ota. Nothing
// calls into this file, so the object would never be pulled out of the
// archive and the weak no-ops would win -- the component's CMakeLists adds
// WHOLE_ARCHIVE for exactly that reason. Verify with `nm` (T, not W) rather
// than by reasoning about it.

#include <atomic>

#include "espos_voice/wyoming_satellite.h"

namespace espos_voice {

// Set by WyomingSatellite's constructor/destructor. A firmware runs one
// satellite; a second would simply replace the first here, and the hook then
// parks that one. Atomic because the OTA task reads it while the app task may
// still be constructing.
std::atomic<WyomingSatellite*> g_ota_quiesce_target{nullptr};

}  // namespace espos_voice

extern "C" void espos_ota_quiesce_hook(void) {
  if (auto* sat = espos_voice::g_ota_quiesce_target.load()) {
    sat->ota_quiesce();
  }
}

extern "C" void espos_ota_resume_hook(void) {
  if (auto* sat = espos_voice::g_ota_quiesce_target.load()) {
    sat->ota_resume();
  }
}
