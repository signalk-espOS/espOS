# SPDX-FileCopyrightText: 2026 humppafreak
# SPDX-License-Identifier: Apache-2.0
#
# The real component, not a copy of its sources: espos_tone is pure C plus
# cJSON, so what runs here is what ships.
idf_component_register(
    SRCS test_main.c test_rtttl.c test_tone.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES espos_tone unity
    WHOLE_ARCHIVE)
