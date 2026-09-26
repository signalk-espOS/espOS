# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Consumer sdkconfig lint. Included by the IDF build system for every project
# that has espos_core in its component list, after sdkconfig has been
# generated and before any component CMakeLists.txt runs; the CONFIG_* values
# are plain CMake variables here (build.cmake includes sdkconfig.cmake first,
# the same way IDF's own partition_table/project_include.cmake reads
# CONFIG_PARTITION_TABLE_CUSTOM).
#
# A firmware that includes cmake/espos_project.cmake inherits every value
# checked below from sdkconfig.d/espos.defaults and never trips this. It is
# for the project that installs espOS's components from the registry and
# writes its own CMakeLists.txt: without the shared defaults its sdkconfig is
# IDF's, and the four settings below are the ones whose absence does not show
# at build time but as a device that wedges or overflows a stack in the
# field. Each failure names the exact line to add.
#
# Every check skips when its value is not in this sdkconfig at all (another
# target, a component not in the build) rather than guess.

if(NOT DEFINED CONFIG_IDF_TARGET OR CONFIG_IDF_TARGET STREQUAL "linux")
    # Host tests run on pthread stacks and have no radio; nothing here applies.
    return()
endif()

set(_espos_lint_fix
    "Add the line to your sdkconfig.defaults and delete the stale sdkconfig so the "
    "defaults are re-applied — or include espOS's cmake/espos_project.cmake, whose "
    "espos_project_prologue() puts sdkconfig.d/espos.defaults on SDKCONFIG_DEFAULTS "
    "for you.")
string(CONCAT _espos_lint_fix ${_espos_lint_fix})

# Findings accumulate and are reported together at the end. Failing on the
# first one makes a consumer without the prologue fix a setting, reconfigure,
# and be told about the next -- three or four rounds before the build starts.
# Captured at include time: inside a function or later in the file
# CMAKE_CURRENT_LIST_DIR is whatever is being processed then, not this file.
set(ESPOS_CORE_PARTITIONS "${CMAKE_CURRENT_LIST_DIR}/partitions" CACHE INTERNAL
    "espOS's bundled partition tables, shipped inside espos_core")

set(_espos_lint_problems "")
set(_espos_lint_lines "")
macro(_espos_lint_report why line)
    string(APPEND _espos_lint_problems "  - ${why}\n")
    string(APPEND _espos_lint_lines "    ${line}\n")
endmacro()

# WiFi/IP events and FreeRTOS timers run the state machine, JSON building and
# SSE sends; the IDF defaults (2304 / 2048) are too tight (espos.defaults).
foreach(_espos_stack CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH)
    if(DEFINED ${_espos_stack} AND ${_espos_stack} LESS 4096)
        _espos_lint_report(
            "${_espos_stack}=${${_espos_stack}}: espOS runs its WiFi/SignalK state machines, JSON building and SSE sends on that task and needs at least 4096 (a smaller stack overflows on the first busy event, not at boot)."
            "${_espos_stack}=4096")
    endif()
endforeach()

if(CONFIG_IDF_TARGET STREQUAL "esp32p4")
    # From espos.defaults.esp32p4: without PSRAM, esp_hosted's startup
    # allocations leave so little internal RAM that FreeRTOS cannot allocate
    # the timer task's stack when the scheduler starts, and every boot panics
    # within seconds. Seen on a Waveshare P4 PoE board.
    if(NOT CONFIG_SPIRAM)
        _espos_lint_report(
            "CONFIG_SPIRAM is off on the ESP32-P4. esp_hosted's startup allocations leave internal RAM so short that FreeRTOS cannot allocate its timer task's stack, and the board panics within seconds of every boot (\"assert failed: vApplicationGetTimerTaskMemory port_common.c:97\"). CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM is dropped without it as well."
            "CONFIG_SPIRAM=y")
    endif()

    # From espos.defaults.esp32p4: "Keep the hosted transport mempool — the
    # transport's large DMA buffer pool — out of internal RAM [...]
    # CONSTRAINT: only safe with 64-byte L2 cache lines — the 1600-byte
    # transport stride is 64-aligned but NOT 128-aligned, so with
    # CONFIG_CACHE_L2_CACHE_LINE_128B the SDIO driver rejects PSRAM buffers
    # (ESP_ERR_INVALID_ARG; esp-hosted-mcu#219)."
    if(CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM AND DEFINED CONFIG_CACHE_L2_CACHE_LINE_64B
       AND NOT CONFIG_CACHE_L2_CACHE_LINE_64B)
        _espos_lint_report(
            "CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y without CONFIG_CACHE_L2_CACHE_LINE_64B=y. The hosted transport's 1600-byte buffer stride is 64-aligned but not 128-aligned, so with 128-byte L2 cache lines the SDIO driver rejects the PSRAM buffers (ESP_ERR_INVALID_ARG, esp-hosted-mcu#219) and the co-processor link wedges. A 256 KB L2 cache forces 128-byte lines; use 128 KB."
            "CONFIG_CACHE_L2_CACHE_LINE_64B=y")
    endif()

    # From espos.defaults.esp32p4: "IDF defaults this to 6, but silently
    # raises it to 16 as soon as a project enables PSRAM
    # (SPIRAM_TRY_ALLOCATE_WIFI_LWIP) [...]. A window of 16 overruns the
    # SDIO Rx path on this transport: it wedges under sustained inbound TCP
    # [...] (espressif/esp-hosted-mcu#184). Measured on a Waveshare 7B with
    # repeated ~30 KB HTTP reads: at 16 the link wedged after 85 requests /
    # 77 s; at 6 it survived 400 consecutive requests over 283 s."
    if(CONFIG_SPIRAM AND DEFINED CONFIG_WIFI_RMT_RX_BA_WIN AND NOT CONFIG_WIFI_RMT_RX_BA_WIN EQUAL 6)
        _espos_lint_report(
            "CONFIG_SPIRAM=y with CONFIG_WIFI_RMT_RX_BA_WIN=${CONFIG_WIFI_RMT_RX_BA_WIN}. Enabling PSRAM makes IDF raise the remote radio's receive block-ack window to 16, which overruns the SDIO Rx path to the C6 under sustained inbound TCP: the link wedges (\"H_SDIO_DRV: task still writing Rx data to queue!\") and WiFi stays dead until reboot (esp-hosted-mcu#184). Measured: 16 wedged after 85 requests, 6 survived 400. The knob is WIFI_RMT_*, not ESP_WIFI_RX_BA_WIN — the radio is remote."
            "CONFIG_WIFI_RMT_RX_BA_WIN=6")
    endif()
endif()

# Settings that are not a judgement call: espOS's own code or build depends on
# each, and a consumer without the prologue gets none of them. Kept in one
# table so it can be diffed against sdkconfig.d/espos.defaults, which
# tools/check_sdkconfig_lint.py does in CI -- the lint and the defaults are two
# expressions of one truth and must not drift apart.
#
# CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH is the reason this table exists: without
# it, espos_httpd's api_coredump.c fails to find esp_core_dump.h roughly 900
# files into the build, a long way from anything that names the cause.
foreach(_espos_need
        CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
        CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
        CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT
        CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT
        CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME
        CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES)
    if(NOT ${_espos_need})
        if(_espos_need STREQUAL "CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH")
            _espos_lint_report(
                "CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH is off. espos_httpd serves the stored core dump at /api/v1/system/coredump and includes esp_core_dump.h, which IDF only provides when this is on; without it the build fails deep in espos_httpd rather than here."
                "CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y")
        elseif(_espos_need STREQUAL "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE")
            _espos_lint_report(
                "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is off. espos_ota marks an image valid only after it has run; without rollback a bad update is permanent and the device needs a USB flash."
                "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y")
        else()
            _espos_lint_report(
                "${_espos_need} is off. espOS signs every image so a device accepts an OTA only from the key it was flashed with; with signing off, the firmware installs over USB and then rejects every update, on the device, long after the build went green."
                "${_espos_need}=y")
        endif()
    endif()
endforeach()

# The UI, the config store and OTA all need somewhere to live. A table without
# them builds and then fails at runtime or at the first update, so say it here.
if(CONFIG_PARTITION_TABLE_SINGLE_APP OR CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE)
    # Name the table's real path. It ships inside this component, so the same
    # message works from a checkout and from managed_components/ -- telling a
    # registry consumer to look in "partitions/" was advice for a directory
    # they do not have, and they met a bare ninja error instead.
    #
    # The consumer has to name it themselves: CONFIG_PARTITION_TABLE_CUSTOM_FILENAME
    # is read while sdkconfig is generated, which is before any component CMake
    # runs, so this file can point at a table but cannot select one.
    _espos_lint_report(
        "the partition table is IDF's single-app default: one app slot and no 'storage'. espOS needs two OTA slots (an update stages into the passive one) and a storage partition for the web UI. Copy a bundled table next to your CMakeLists.txt -- ${ESPOS_CORE_PARTITIONS}/{4,8,16}mb.csv -- and name it below. Match CONFIG_ESPTOOLPY_FLASHSIZE_* to the table you pick, or the image will not fit the chip."
        "CONFIG_PARTITION_TABLE_CUSTOM=y\n    CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions.csv\"")
endif()

# A table and a flash size that disagree produce an image the chip cannot
# hold, and the flash fails partway through writing it. IDF catches it later
# and says nothing about espOS's tables, so say it here where the table was
# just chosen.
#
# espOS's tables leave offsets blank (gen_esp32part assigns them) and size in
# K/M, so sum the sizes rather than reading an end offset -- an earlier
# version of this check looked for hex offsets, matched nothing, and silently
# never fired.
if(CONFIG_PARTITION_TABLE_CUSTOM AND CONFIG_PARTITION_TABLE_CUSTOM_FILENAME AND CONFIG_ESPTOOLPY_FLASHSIZE)
    get_filename_component(_espos_table "${CONFIG_PARTITION_TABLE_CUSTOM_FILENAME}"
                           ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(EXISTS "${_espos_table}")
        file(STRINGS "${_espos_table}" _espos_rows REGEX "^[^#]+,")
        set(_espos_total 0)
        foreach(_espos_row IN LISTS _espos_rows)
            string(REPLACE " " "" _espos_row "${_espos_row}")
            # Match the 5th field directly. Splitting on "," and counting does
            # NOT work: list(LENGTH) drops empty elements (CMP0007), and these
            # tables leave the offset column blank, so every row looked like it
            # had four fields and the check silently never fired.
            if(_espos_row MATCHES "^[^,]*,[^,]*,[^,]*,[^,]*,([^,]+)")
                set(_espos_size "${CMAKE_MATCH_1}")
                set(_espos_bytes 0)
                if(_espos_size MATCHES "^([0-9]+)K$")
                    math(EXPR _espos_bytes "${CMAKE_MATCH_1} * 1024")
                elseif(_espos_size MATCHES "^([0-9]+)M$")
                    math(EXPR _espos_bytes "${CMAKE_MATCH_1} * 1048576")
                elseif(_espos_size MATCHES "^0x[0-9a-fA-F]+$")
                    math(EXPR _espos_bytes "${_espos_size}")
                elseif(_espos_size MATCHES "^[0-9]+$")
                    set(_espos_bytes ${_espos_size})
                endif()
                math(EXPR _espos_total "${_espos_total} + ${_espos_bytes}")
            endif()
        endforeach()
        # The table itself starts at CONFIG_PARTITION_TABLE_OFFSET (0x8000 by
        # default): bootloader and table sit below the first partition.
        set(_espos_base 32768)
        if(CONFIG_PARTITION_TABLE_OFFSET)
            set(_espos_base ${CONFIG_PARTITION_TABLE_OFFSET})
        endif()
        math(EXPR _espos_end "${_espos_total} + ${_espos_base} + 4096")
        string(REGEX REPLACE "MB$" "" _espos_mb "${CONFIG_ESPTOOLPY_FLASHSIZE}")
        if(_espos_total GREATER 0 AND _espos_mb MATCHES "^[0-9]+$")
            math(EXPR _espos_cap "${_espos_mb} * 1024 * 1024")
            if(_espos_end GREATER _espos_cap)
                math(EXPR _espos_need "(${_espos_end} + 1048575) / 1048576")
                # Round up to a flash size that exists.
                set(_espos_pick 0)
                foreach(_espos_try 2 4 8 16 32)
                    if(_espos_pick EQUAL 0 AND NOT _espos_try LESS _espos_need)
                        set(_espos_pick ${_espos_try})
                    endif()
                endforeach()
                _espos_lint_report(
                    "${CONFIG_PARTITION_TABLE_CUSTOM_FILENAME} needs about ${_espos_need} MB but CONFIG_ESPTOOLPY_FLASHSIZE is ${CONFIG_ESPTOOLPY_FLASHSIZE}, so the table runs past the end of the chip and the flash fails partway through writing it."
                    "CONFIG_ESPTOOLPY_FLASHSIZE_${_espos_pick}MB=y")
            endif()
        endif()
    endif()
endif()

if(_espos_lint_problems)
    message(FATAL_ERROR
        "espos_core: this project's sdkconfig is missing settings espOS depends on.\n\n"
        "${_espos_lint_problems}\n"
        "Add these lines:\n\n"
        "${_espos_lint_lines}\n"
        "${_espos_lint_fix}")
endif()

unset(_espos_lint_fix)
unset(_espos_lint_problems)
unset(_espos_lint_lines)
unset(_espos_need)
unset(_espos_stack)
unset(_espos_table)
unset(_espos_rows)
unset(_espos_cols)
unset(_espos_end)

# ---------------------------------------------------------------------------
# espos_has_component(<var> <bare-name>)
#
# Is a component in this build, whatever it is called?
#
# BUILD_COMPONENTS holds CMake TARGET names, and the component manager
# namespaces those on install: a registry copy of espos_wifi builds as
# `signalk-espos__espos_wifi`, while an in-tree checkout builds as plain
# `espos_wifi`. So `if(espos_wifi IN_LIST comps)` is true in the tree and
# silently FALSE for every registry install -- and silence is the problem:
# espos_core's optional stages simply compiled out, so espos_start() brought up
# no WiFi, no SignalK client and no OTA on a firmware that had linked all three,
# with no error and no warning (espOS #138, found downstream in
# BoatHacks/signalk-espos-8relay#11).
#
# espOS's own CI could not catch it, because the from_registry example uses
# override_path and therefore builds under the bare names too.
#
# COMPONENT_NAME is the bare name in both spellings, so comparing against that
# resolves either. IDF's own __component_get_target() does exactly this
# (tools/cmake/component.cmake: it falls back to matching COMPONENT_NAME when a
# name is not a known target), which is why idf_component_get_property() and
# idf_component_optional_requires() accept bare names and only raw
# `IN_LIST BUILD_COMPONENTS` does not.
# An optional third argument receives the name as this build actually spells it,
# so a caller that then needs idf_component_get_property() passes THAT rather
# than the bare name: that function raises a FATAL_ERROR on a name it cannot
# resolve, and it resolves a bare name only once the component has been
# registered -- which is not guaranteed at the point these run.
function(espos_has_component var name)
    idf_build_get_property(_espos_comps BUILD_COMPONENTS)
    foreach(_espos_t ${_espos_comps})
        # Cheap path first: an in-tree build matches on the nose.
        if(_espos_t STREQUAL name)
            set(${var} TRUE PARENT_SCOPE)
            if(ARGC GREATER 2)
                set(${ARGV2} ${_espos_t} PARENT_SCOPE)
            endif()
            return()
        endif()
        # A namespaced target ends in `__<bare name>`. Compare the suffix rather
        # than stripping a fixed `signalk-espos__`, so a fork or a mirror
        # published under another namespace keeps working.
        if(_espos_t MATCHES "__${name}$")
            set(${var} TRUE PARENT_SCOPE)
            if(ARGC GREATER 2)
                set(${ARGV2} ${_espos_t} PARENT_SCOPE)
            endif()
            return()
        endif()
    endforeach()
    set(${var} FALSE PARENT_SCOPE)
    if(ARGC GREATER 2)
        set(${ARGV2} "" PARENT_SCOPE)
    endif()
endfunction()

# espos_optional_requires(<req_type> <bare-name>...)
#
# idf_component_optional_requires() that works for a namespaced install.
#
# IDF's own version has the same defect this file's helper exists for: it tests
# `req IN_LIST build_components` against TARGET names
# (tools/cmake/component.cmake), so an optional dependency named by its bare name
# is silently skipped when the component is installed from the registry. That is
# how espos_n2k's `GET /api/v1/n2k` became a 404 on registry installs -- the
# optional requirement on espos_httpd did not link, so its headers were not on
# the include path, so __has_include("espos_httpd.h") answered no and the handler
# compiled to a stub (espOS #138).
#
# Names that already carry a namespace (espressif__cjson) pass through: they are
# matched literally by the same helper.
function(espos_optional_requires req_type)
    foreach(_req ${ARGN})
        espos_has_component(_present ${_req} _real)
        if(_present)
            # ${_real}, not ${_req}: idf_component_get_property() raises a
            # FATAL_ERROR on a name it cannot resolve, and its bare-name fallback
            # only works once that component has been registered -- not
            # guaranteed here. Verified: passing the bare name failed with
            # "Failed to resolve component 'espos_sk'".
            idf_component_get_property(_req_lib ${_real} COMPONENT_LIB)
            target_link_libraries(${COMPONENT_LIB} ${req_type} ${_req_lib})
        endif()
    endforeach()
endfunction()
