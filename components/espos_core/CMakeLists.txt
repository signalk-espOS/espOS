# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# espos_core: one call brings espOS up in the right order (include/espos.h).
# The core components are hard requirements; SignalK, OTA and BLE are started
# only when the project builds them — decided here, at configure time, from
# the component list, so the application never spells the list itself.
idf_build_get_property(target IDF_TARGET)
set(reqs espos_log espos_config espos_health espos_httpd espos_net espos_event espos_time)

# WiFi is part of the core set on every chip with a radio (or, on the ESP32-P4,
# a co-processor) and absent on the 802.15.4-only H-series, where esp_wifi does
# not exist and espos_wifi cannot build. espos_project_prologue() records the
# decision as the ESPOS_WIFI build property and excludes the component; a
# project that bypasses the prologue gets the same rule from the target name.
# A hard REQUIRES rather than espos_optional_requires(): under a
# consumer's MINIMAL_BUILD only what is required gets compiled, and "WiFi
# vanished because nothing asked for it" is not a failure mode a firmware
# should have. Everything above the seam depends on espos_net, not on this.
idf_build_get_property(_espos_wifi ESPOS_WIFI)
if("${_espos_wifi}" STREQUAL "")
    set(_espos_no_wifi_targets esp32h2 esp32h21 esp32h4)
    if(target IN_LIST _espos_no_wifi_targets)
        set(_espos_wifi OFF)
    else()
        set(_espos_wifi ON)
    endif()
endif()
if(_espos_wifi)
    list(APPEND reqs espos_wifi)
endif()

idf_component_register(
    SRCS src/espos_core.c
    INCLUDE_DIRS include
    REQUIRES ${reqs})

# Optional stages: link them only when they are in the build, and tell the
# source which ones are, so espos_start() can call into them without an
# #include that would fail in a firmware built without them.
# Namespace-tolerant component checks (espOS #138); guarded against a second
# include, which project_include.cmake has already done in a normal build.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/espos_components.cmake")

espos_optional_requires(PRIVATE espos_sk espos_ota espos_ble espos_prov espos_eth espos_power)
# espos_has_component(), not `IN_LIST BUILD_COMPONENTS`: that list holds target
# names and the component manager namespaces them on install, so the bare name
# never matched for a registry install and every stage below silently compiled
# out (espOS #138). The helper is in project_include.cmake.
espos_has_component(_have_wifi espos_wifi)
if(_have_wifi)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOS_HAVE_WIFI=1)
endif()
espos_has_component(_have_sk espos_sk)
if(_have_sk)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOS_HAVE_SK=1)
endif()
espos_has_component(_have_ota espos_ota)
if(_have_ota)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOS_HAVE_OTA=1)
endif()
# espos_ble registers only its protocol codec on the linux target; there is
# no espos_ble_start() to call there. On chips the gateway also needs
# Bluedroid, which the source checks (CONFIG_BT_BLUEDROID_ENABLED) — a
# firmware may link the component without enabling the stack.
espos_has_component(_have_ble espos_ble)
if(_have_ble AND NOT target STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOS_HAVE_BLE=1)
endif()

# Same shape for BLE provisioning. It also gates on CONFIG_ESPOS_PROV in the
# source: unlike the gateway, linking the component is not the same as wanting
# it started -- it suspends BLE scanning while it runs.
espos_has_component(_have_prov espos_prov)
if(_have_prov AND NOT target STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOS_HAVE_PROV=1)
endif()

# Ethernet, same shape. The source additionally needs CONFIG_ETH_USE_ESP32_EMAC
# (a chip without an internal EMAC links only espos_eth's stubs) and the
# CONFIG_ESPOS_ETH switch.
espos_has_component(_have_eth espos_eth)
if(_have_eth AND NOT target STREQUAL "linux")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOS_HAVE_ETH=1)
endif()

# The deep-sleep duty cycle, same shape. Started last, and on the linux
# target too: there it never sleeps, but its status endpoint still answers.
espos_has_component(_have_power espos_power)
if(_have_power)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ESPOS_HAVE_POWER=1)
endif()

# espOS's own version, as distinct from the application's PROJECT_VER: in the
# tree it is version.txt two levels up; a copy installed from the component
# registry has no such file and carries the version in its manifest instead.
set(espos_version_file "${CMAKE_CURRENT_LIST_DIR}/../../version.txt")
if(EXISTS "${espos_version_file}")
    file(READ "${espos_version_file}" espos_version)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${espos_version_file}")
else()
    file(READ "${CMAKE_CURRENT_LIST_DIR}/idf_component.yml" espos_manifest)
    string(REGEX MATCH "\nversion: \"([^\"]+)\"" _ "${espos_manifest}")
    set(espos_version "${CMAKE_MATCH_1}")
endif()
string(STRIP "${espos_version}" espos_version)
if(NOT espos_version)
    set(espos_version "0.0.0-unknown")
endif()
# The application's name and version, the way esp_app_format reads them.
idf_build_get_property(project_name PROJECT_NAME)
idf_build_get_property(project_ver PROJECT_VER)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESPOS_VERSION="${espos_version}"
    ESPOS_PROJECT_NAME="${project_name}"
    ESPOS_PROJECT_VER="${project_ver}")
