# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Everything before project() is the shared prologue: the IDF version policy,
# espOS's sdkconfig defaults and partition table, the app-signing key. Used
# outside the espOS tree the include becomes
# "${CMAKE_CURRENT_LIST_DIR}/espos/cmake/espos_project.cmake" with espOS as
# the `espos/` submodule (docs/development.md).
cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/espos_project.cmake")
# espos_power and espos_eth are optional, so they have to be named. espos_eth
# is the ESP32-P4 PoE board's network; on a chip without an EMAC it builds as
# stubs and the device uses WiFi.
espos_project_prologue(NAME "duty_cycle" COMPONENTS espos_power espos_eth)
project(duty_cycle)
# The committed web UI bundle -> LittleFS "storage" image.
espos_project_ui_partition()
