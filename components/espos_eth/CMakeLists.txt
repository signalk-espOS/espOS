# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
idf_build_get_property(target IDF_TARGET)
set(srcs src/espos_eth.c)
set(priv_reqs espos_net)
# Target, not CONFIG_ETH_USE_ESP32_EMAC: component CMakeLists are evaluated
# during early expansion, before sdkconfig exists, so a CONFIG_ guard here is
# empty on the first pass (the reason espos_ble does the same). The source is
# #if'd on the symbol instead, so a chip without an EMAC -- ESP32-C3, C6, S3 --
# compiles one small object of ESP_ERR_NOT_SUPPORTED stubs.
if(NOT target STREQUAL "linux")
    list(APPEND priv_reqs esp_eth esp_netif esp_event)
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    PRIV_REQUIRES ${priv_reqs})
