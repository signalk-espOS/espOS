# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# The follower is compiled directly: it is driver-free by design, while the
# component around it needs TinyUSB and the SDMMC host the linux target has not
# got.
set(src "${CMAKE_CURRENT_LIST_DIR}/../../../../components/espos_usb_storage")
idf_component_register(
    SRCS test_main.c test_follower.cpp "${src}/src/follower.cpp"
    INCLUDE_DIRS "." "${src}/include"
    PRIV_REQUIRES unity
    WHOLE_ARCHIVE)
