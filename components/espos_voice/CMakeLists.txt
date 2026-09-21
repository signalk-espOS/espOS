# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Wyoming-protocol voice satellite: a TCP server that lets a
# signalk-wyoming / Home Assistant orchestrator play TTS to, and stream
# mic audio from, whatever espos_audio::AudioDriver the application
# supplies, with on-device wake word via esp-sr WakeNet.
#
# esp-sr is a hard requirement where it exists: WyomingSatellite owns a
# WakeEngine directly. Making it optional would mean an abstract wake
# interface and a null implementation — worth doing if a board ever wants
# the satellite without on-device wake, but not before there is one.
#
# esp-sr only supports esp32s3 and esp32p4 (see idf_component.yml), so on
# every other target this component registers as an empty stub rather than
# failing the build. Applications on those targets simply have no voice.
idf_build_get_property(target IDF_TARGET)
if(target STREQUAL "esp32s3" OR target STREQUAL "esp32p4")
    idf_component_register(
        SRCS src/wyoming_satellite.cpp src/wake_engine.cpp
             src/ota_quiesce.cpp
             src/protocol/events.cpp src/protocol/framing.cpp
        INCLUDE_DIRS include
        REQUIRES espos_audio
        # espos_health for conditions: a wake service that goes away otherwise
        # leaves a panel that silently stops waking, which is indistinguishable
        # from a dead mic. Reporting that is core behaviour, not something each
        # application should re-derive from a callback -- but it is no reason
        # to depend on SignalK, which is what espos_sk here used to mean. What
        # becomes of a condition is the sink's business: espos_sk publishes a
        # notification when a firmware has it, and a firmware without it can
        # still light an LED.
        PRIV_REQUIRES lwip espressif__cjson espressif__esp-sr espos_health
                      esp_timer)
else()
    idf_component_register()
endif()

# ota_quiesce.cpp holds the STRONG definitions of espos_ota's weak
# espos_ota_quiesce_hook()/espos_ota_resume_hook(). Nothing in this component
# calls into that file, so without WHOLE_ARCHIVE the linker never pulls the
# object out of libespos_voice.a, the weak no-ops win, and an OTA silently
# keeps starving the AFE -- a failure that builds, links and only shows up as a
# watchdog abort on a device. Same trap as espos_time's wallclock hooks (#49).
# Verify with `nm` (T, not W), never by reasoning.
if(target STREQUAL "esp32s3" OR target STREQUAL "esp32p4")
    target_link_libraries(${COMPONENT_LIB} INTERFACE
                          "-Wl,--whole-archive" ${COMPONENT_LIB} "-Wl,--no-whole-archive")
endif()
