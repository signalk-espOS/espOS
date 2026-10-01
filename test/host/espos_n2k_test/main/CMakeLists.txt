# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Compile the pure parts directly rather than depending on espos_n2k: the
# component needs the TWAI peripheral and lwIP, which the linux target has
# not got. The codec needs neither — that is the point of it no longer naming
# the driver's frame type — and neither does the TX slot ring, which is why
# src/ is on the include path: tx_slot_ring.h is header-only and IDF-free.
set(n2k_src "${CMAKE_CURRENT_LIST_DIR}/../../../../components/espos_n2k")
idf_component_register(
    SRCS test_main.c test_candump.cpp test_tx_slots.cpp "${n2k_src}/src/candump_format.cpp"
    INCLUDE_DIRS "." "${n2k_src}/include" "${n2k_src}/src"
    PRIV_REQUIRES unity
    WHOLE_ARCHIVE)
