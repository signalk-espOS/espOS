# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Compile the pure parts directly rather than depending on espos_n2k: the
# component needs the TWAI peripheral and lwIP, which the linux target has
# not got. The codec needs neither — that is the point of it no longer naming
# the driver's frame type — and neither does the TX slot ring, which is why
# src/ is on the include path: tx_slot_ring.h is header-only and IDF-free.
#
# The vendored NMEA2000 library builds here too, on a simulated bus with a
# clock the tests drive (test_node_bus.cpp): espos_n2k::Node leaves the
# protocol to it, so its behaviour is pinned down where an update would show.
set(n2k_src "${CMAKE_CURRENT_LIST_DIR}/../../../../components/espos_n2k")
set(n2klib "${n2k_src}/third_party/NMEA2000/src")
idf_component_register(
    SRCS test_main.c test_candump.cpp test_tx_slots.cpp test_listeners.cpp
         test_node_identity.cpp test_node_bus.cpp
         "${n2k_src}/src/candump_format.cpp"
         "${n2klib}/NMEA2000.cpp" "${n2klib}/N2kMsg.cpp" "${n2klib}/N2kTimer.cpp"
         "${n2klib}/N2kStream.cpp" "${n2klib}/N2kGroupFunction.cpp"
         "${n2klib}/N2kGroupFunctionDefaultHandlers.cpp" "${n2klib}/N2kMessages.cpp"
    INCLUDE_DIRS "." "${n2k_src}/include" "${n2k_src}/src" "${n2klib}"
    PRIV_REQUIRES unity esp_timer
    WHOLE_ARCHIVE)
