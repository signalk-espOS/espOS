# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Host (linux target) test that a firmware's own UI bundle, named with
# ESPOS_HTTPD_UI_DIR before project(), is what espos_httpd builds into the
# app image instead of its own ui-dist/ (espOS #297). Build & run:
#
#   ../../../scripts/build.sh --preview set-target linux && ../../../scripts/build.sh build && ./build/espos_httpd_ui_dir_test.elf
#
cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../components")
# Relative on purpose: it resolves against the project directory.
idf_build_set_property(ESPOS_HTTPD_UI_DIR "ui-fixture")
project(espos_httpd_ui_dir_test)
