/*
 * SPDX-FileCopyrightText: 2026 humppafreak
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_rtttl_parse() / espos_tone_library_parse() -- a tune typed into a
 * settings page.
 *
 * The tone library is a table setting, so every byte of it is whatever the
 * device's owner (or anyone with the config API) typed. The parser walks it
 * with raw pointers and scales numbers out of it into frequencies and
 * durations, which is where the octave bug the unit tests now pin (a huge
 * o= looping once per octave) came from. This checks the rest:
 *
 *   - nothing is written past `max` notes;
 *   - every pitch is in the range the octave clamp promises (c9 at most) and
 *     every duration fits the type without having wrapped;
 *   - espos_rtttl_tone_at() agrees with the notes it was given;
 *   - the same bytes as a library table parse without a fault and only
 *     produce rows the header promises (non-empty name, at least one note).
 *
 * Both halves are pure (libc and cJSON), so this builds with the host
 * compiler like the other harnesses.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "espos_rtttl.h"
#include "espos_tone.h"

#define MAX_NOTES ESPOS_RTTTL_MAX_NOTES
#define C9_HZ     8372 /* b#8, the highest pitch the octave clamp allows */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* A tune is a config string; kilobytes say nothing a few hundred do not. */
    if (size > 4096) {
        size = 4096;
    }
    char *in = (char *)malloc(size + 1);
    if (!in) {
        return 0;
    }
    memcpy(in, data, size);
    in[size] = '\0';

    /* --- 1. the parser, into a heap block of exactly MAX_NOTES ----------
     * so a one-note overrun is an ASan report, not a silent write. */
    espos_rtttl_note_t *notes = (espos_rtttl_note_t *)malloc(MAX_NOTES * sizeof(*notes));
    if (!notes) {
        free(in);
        return 0;
    }
    size_t n = espos_rtttl_parse(in, notes, MAX_NOTES);
    if (n > MAX_NOTES) {
        abort();
    }
    uint32_t total = 0;
    for (size_t i = 0; i < n; i++) {
        if (notes[i].freq_hz > C9_HZ) {
            abort(); /* the octave clamp leaked */
        }
        total += notes[i].duration_ms;
    }
    if (espos_rtttl_duration_ms(notes, n) != total) {
        abort();
    }

    /* --- 2. tone_at walks the same notes ------------------------------- */
    uint32_t t = 0;
    for (size_t i = 0; i < n; i++) {
        if (notes[i].duration_ms == 0) {
            continue; /* a zero-length note is never current */
        }
        uint16_t hz = 0;
        bool on = espos_rtttl_tone_at(notes, n, t, &hz);
        if (on != (notes[i].freq_hz != 0) || (on && hz != notes[i].freq_hz)) {
            abort();
        }
        t += notes[i].duration_ms;
    }
    uint16_t hz = 0x5A5A;
    if (espos_rtttl_tone_at(notes, n, total, &hz) || hz != 0x5A5A) {
        abort(); /* sounding, or touched the pitch, past the end */
    }

    /* --- 3. a smaller max is honoured ---------------------------------- */
    if (n > 1) {
        espos_rtttl_note_t *few = (espos_rtttl_note_t *)malloc((n - 1) * sizeof(*few));
        if (few) {
            if (espos_rtttl_parse(in, few, n - 1) != n - 1) {
                abort();
            }
            free(few);
        }
    }
    free(notes);

    /* --- 4. the same bytes as a library table -------------------------- */
    enum { MAX_TONES = 4 };
    espos_tone_t *tones = (espos_tone_t *)malloc(MAX_TONES * sizeof(*tones));
    if (tones) {
        size_t nt = espos_tone_library_parse(in, tones, MAX_TONES);
        if (nt > MAX_TONES) {
            abort();
        }
        for (size_t i = 0; i < nt; i++) {
            size_t len = strnlen(tones[i].name, sizeof(tones[i].name));
            if (len == 0 || len > ESPOS_TONE_NAME_MAX || tones[i].n_notes == 0 || tones[i].n_notes > MAX_NOTES) {
                abort();
            }
            if (espos_tone_library_find(tones, nt, tones[i].name) > (int)i) {
                abort(); /* find must return the first match */
            }
        }
        free(tones);
    }

    free(in);
    return 0;
}
