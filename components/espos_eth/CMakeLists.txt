# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
idf_build_get_property(target IDF_TARGET)
set(srcs src/espos_eth.c)
set(priv_reqs espos_net)
# Target, not the controller choice: component CMakeLists are evaluated
# during early expansion, before sdkconfig exists, so a CONFIG_ guard here is
# empty on the first pass (the reason espos_ble does the same). The source is
# #if'd on the controller choice instead, so a build with none -- the default
# on a chip without an EMAC -- compiles one small object of
# ESP_ERR_NOT_SUPPORTED stubs.
# The W5500 driver left the IDF core in 6.0 (espressif/w5500, see
# idf_component.yml); it is required on every target but linux, like the
# rest, so the controller choice in menu "espOS Ethernet" never changes the
# component graph.
if(NOT target STREQUAL "linux")
    list(APPEND priv_reqs esp_eth esp_netif esp_event esp_driver_spi esp_driver_gpio espressif__w5500)
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    PRIV_REQUIRES ${priv_reqs})
