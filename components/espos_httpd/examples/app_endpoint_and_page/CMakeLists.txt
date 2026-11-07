# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# A firmware's own REST endpoints and SSE event on the espOS HTTP server.
# The build side is the same as every espOS project's — the shared prologue
# does the IDF check, the sdkconfig defaults, the partition table, the
# components and the signing key.
cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/espos_project.cmake")
espos_project_prologue(NAME "app_endpoint_and_page")
project(app_endpoint_and_page)
# The stock UI bundle. A firmware with a page of its own names the dist-gz/
# of its own UI build with idf_build_set_property(ESPOS_HTTPD_UI_DIR ...)
# before project() (README, "A page in the web UI").
espos_project_ui_partition()
