/*
 * SPDX-FileCopyrightText: 2026 humppafreak
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_rtttl — a minimal RTTTL (Ring Tone Text Transfer Language) parser.
 *
 * Turns "name:d=4,o=5,b=63:c,8e,g5,2p,..." into a list of notes a buzzer can
 * play. Pure logic: no hardware, no clock, nothing beyond libc, so it runs
 * on the host target and in the fuzzer exactly as it runs on the chip.
 *
 * The parser is lenient on purpose. A tune is typed into a settings page by
 * whoever owns the device, and a stray character should cost one note, not
 * the whole tune: an unrecognised token is skipped to the next comma. Every
 * number is capped rather than wrapped, and the octave is clamped to 8 (c8 is
 * already ~4.2 kHz, above what a piezo buzzer reproduces well).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The most notes one tune parses into; later notes are dropped. */
#define ESPOS_RTTTL_MAX_NOTES 64

typedef struct {
    uint16_t freq_hz;     /**< 0 = rest ('p') */
    uint16_t duration_ms; /**< capped at UINT16_MAX */
} espos_rtttl_note_t;

/**
 * Parses the note section of `rtttl` into `out` (at most `max` notes).
 *
 * The leading name is ignored. A missing or invalid default section
 * (d=/o=/b=) falls back to RTTTL's own defaults (4, 5, 63); a string with no
 * ':' at all is read as notes with those defaults. Unrecognised note tokens
 * are skipped.
 *
 * @return the number of notes written; 0 if there are no playable notes at
 *         all (NULL, empty, or every token unrecognised).
 */
size_t espos_rtttl_parse(const char *rtttl, espos_rtttl_note_t *out, size_t max);

/** Sum of every note's duration. */
uint32_t espos_rtttl_duration_ms(const espos_rtttl_note_t *notes, size_t n);

/**
 * Whether a tone sounds at `t_ms` into one (non-looping) pass, and at what
 * frequency. False during a rest and from the end of the sequence on;
 * *freq_hz is left unchanged whenever this returns false.
 */
bool espos_rtttl_tone_at(const espos_rtttl_note_t *notes, size_t n, uint32_t t_ms, uint16_t *freq_hz);

#ifdef __cplusplus
}
#endif
