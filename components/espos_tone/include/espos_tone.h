/*
 * SPDX-FileCopyrightText: 2026 humppafreak
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_tone — named event tones and a one-shot player for them.
 *
 * A device that chirps on boot, on a relay switching or on a button press
 * keeps its tunes as a table setting: a JSON array of {"name","rtttl"} rows
 * that the owner edits in the web UI. espos_tone_library_parse() turns that
 * table into tones looked up by name, and espos_tone_player_t plays one of
 * them once.
 *
 * The player owns no hardware and no clock. The caller passes a free-running
 * millisecond clock and drives its own buzzer from the pitch
 * espos_tone_player_step() hands back, typically from the task or timer that
 * already runs its LEDs:
 *
 *     espos_tone_player_t player = { 0 };
 *     espos_tone_player_start(&player, &tones[i], now_ms);
 *     ...
 *     uint16_t hz;
 *     if (espos_tone_player_step(&player, now_ms, &hz)) {
 *         buzzer_set_freq(hz);
 *         buzzer_on();
 *     } else {
 *         buzzer_off();
 *     }
 *
 * That keeps espOS out of LEDC: a timer and channel number belong to the
 * firmware, which is the only one that knows what else it drives with them.
 * Which sound wins when an alarm, a status override and a chirp all want the
 * buzzer is likewise the firmware's policy, not this component's.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "espos_rtttl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest tone name, not counting the NUL. A longer name skips the row. */
#define ESPOS_TONE_NAME_MAX 24
/** A sensible size for a tone library array; not a limit the parser imposes. */
#define ESPOS_TONE_MAX_TONES 24

typedef struct {
    char name[ESPOS_TONE_NAME_MAX + 1];
    espos_rtttl_note_t notes[ESPOS_RTTTL_MAX_NOTES];
    size_t n_notes;
} espos_tone_t;

/**
 * Parses `json`, a JSON array of {"name","rtttl"} objects, into `out`.
 *
 * A row is skipped if its name is missing, empty or longer than
 * ESPOS_TONE_NAME_MAX, or its RTTTL has no playable notes or plays for 0 ms.
 * Malformed JSON, or anything that is not an array, parses as zero tones.
 *
 * @return the number of tones written (at most `max`).
 */
size_t espos_tone_library_parse(const char *json, espos_tone_t *out, size_t max);

/** Index of the tone called `name` in tones[0..n), or -1 (also for NULL or ""). */
int espos_tone_library_find(const espos_tone_t *tones, size_t n, const char *name);

typedef struct {
    espos_tone_t tone;
    bool playing;
    uint32_t start_ms;
    uint32_t len_ms;
} espos_tone_player_t;

/**
 * Starts one pass of `tone` at `now_ms`, replacing anything playing. The tone
 * is copied, so the library it came from may be reparsed while it plays.
 *
 * @return false, and nothing playing, if the tone has no notes.
 */
bool espos_tone_player_start(espos_tone_player_t *p, const espos_tone_t *tone, uint32_t now_ms);

/** Stops at once; a no-op when idle. */
void espos_tone_player_stop(espos_tone_player_t *p);

/**
 * Advances to `now_ms`, which may wrap. True while a note sounds, with its
 * pitch in *freq_hz (unchanged otherwise); false during a rest, when idle,
 * and from the end of the pass on, which also stops the player.
 */
bool espos_tone_player_step(espos_tone_player_t *p, uint32_t now_ms, uint16_t *freq_hz);

#ifdef __cplusplus
}
#endif
