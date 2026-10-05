# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
# Host unit tests for the candump codec, the TX slot ring, the receiver's
# listener table and the NMEA 2000 node's identity and protocol library.
#   idf.py --preview set-target linux && idf.py build && ./build/espos_n2k_test.elf
cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)
project(espos_n2k_test)
