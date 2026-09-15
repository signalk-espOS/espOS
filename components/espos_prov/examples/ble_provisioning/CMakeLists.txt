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
# espos_prov is optional, so it has to be named: projects on espOS build with
# IDF's MINIMAL_BUILD, and a component nothing requires is not compiled.
#
# espos_ble is deliberately NOT named. The two cannot share a firmware --
# Bluedroid keeps one GAP callback and protocomm takes it while provisioning
# runs (docs/provisioning.md, "It cannot share a firmware with espos_ble").
#
# PARTITIONS: a Bluedroid firmware does not fit the default 4 MB table, whose
# app slots are 1728K -- this one links to about 2.2 MB, and the build fails
# at check_sizes.py rather than at runtime. The same reason the ble_gateway
# example passes this. A device keeps the table it was flashed with, so a
# board with less than 16 MB of flash needs partitions/8mb.csv here instead.
espos_project_prologue(NAME "ble_provisioning"
                       PARTITIONS "${CMAKE_CURRENT_LIST_DIR}/../../../../partitions/16mb.csv"
                       COMPONENTS espos_prov)
project(ble_provisioning)
# The committed web UI bundle -> LittleFS "storage" image.
espos_project_ui_partition()
