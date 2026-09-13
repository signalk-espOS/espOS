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
# espos_n2k is optional, so it has to be named: projects on espOS build with
# IDF's MINIMAL_BUILD, and a component nothing requires is not compiled.
espos_project_prologue(NAME "n2k_candump" COMPONENTS espos_n2k)
project(n2k_candump)
# The committed web UI bundle -> LittleFS "storage" image.
espos_project_ui_partition()
