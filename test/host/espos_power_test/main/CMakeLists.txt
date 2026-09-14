# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Just the policy source, the way espos_time_test does it: what is under test
# is pure C over plain inputs, and the component proper would drag the config
# store, the HTTP server and the network seam along for no coverage.
set(power_src "${CMAKE_CURRENT_LIST_DIR}/../../../../components/espos_power")
idf_component_register(
    SRCS test_main.c test_policy.c "${power_src}/src/power_policy.c"
    INCLUDE_DIRS "." "${power_src}/include"
    PRIV_REQUIRES unity
    WHOLE_ARCHIVE)
