# SPDX-FileCopyrightText: 2026 humppafreak
# SPDX-License-Identifier: Apache-2.0
#
# espos_tone: RTTTL tunes, a named library of them, and a one-shot player
# (docs/tone.md).
#
# Pure C and nothing but cJSON (for the library table), on every target. The
# player hands back the pitch to sound at a given millisecond; driving the
# buzzer is the board's job, so espOS never allocates an LEDC timer or
# channel the firmware also allocates from.
idf_component_register(
    SRCS src/rtttl.c src/tone_library.c src/tone_player.c
    INCLUDE_DIRS include
    PRIV_REQUIRES espressif__cjson)
