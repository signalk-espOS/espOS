/* SPDX-FileCopyrightText: 2026 humppafreak
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_rtttl: the tune parser.
 *
 * A tune is typed by a person into a settings page, so the properties worth
 * pinning are the musical ones (tempo, default and per-note duration and
 * octave, sharps, dots, rests) and the defensive ones: a garbled token costs
 * that note and nothing else, a broken header falls back to RTTTL's defaults,
 * and no number, however long, wraps or takes time proportional to its value.
 */
#include <string.h>

#include "espos_rtttl.h"
#include "unity.h"

#define MAX ESPOS_RTTTL_MAX_NOTES

/* ------------------------------------------------------------- durations */

TEST_CASE("rtttl: duration comes from the tempo and the default duration", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    /* Whole note at 200 bpm = 240000 / 200 = 1200 ms; a 16th = 75 ms. */
    size_t n = espos_rtttl_parse("boot:d=16,o=5,b=200:c,e,g", notes, MAX);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL(75, notes[0].duration_ms);
    TEST_ASSERT_EQUAL(75, notes[1].duration_ms);
    TEST_ASSERT_EQUAL(75, notes[2].duration_ms);
    /* c5, e5, g5 (equal temperament, A4 = 440). */
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);
    TEST_ASSERT_EQUAL(659, notes[1].freq_hz);
    TEST_ASSERT_EQUAL(784, notes[2].freq_hz);
    TEST_ASSERT_EQUAL(225, espos_rtttl_duration_ms(notes, n));
}

TEST_CASE("rtttl: a per-note duration and octave override the defaults", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    size_t n = espos_rtttl_parse("x:d=4,o=4,b=120:8c5", notes, MAX);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);     /* octave 5, not the default 4 */
    TEST_ASSERT_EQUAL(250, notes[0].duration_ms); /* an 8th, not the default quarter */
}

TEST_CASE("rtttl: total duration of a mixed tune, worked out by hand", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    /* 120 bpm: whole = 2000 ms. c = 500, 8p = 250, 2e = 1000,
     * 16g. = 125 * 1.5 = 187.5 -> 188 (rounded), 1a = 2000. */
    size_t n = espos_rtttl_parse("x:d=4,o=5,b=120:c,8p,2e,16g.,1a", notes, MAX);
    TEST_ASSERT_EQUAL(5, n);
    TEST_ASSERT_EQUAL(500, notes[0].duration_ms);
    TEST_ASSERT_EQUAL(250, notes[1].duration_ms);
    TEST_ASSERT_EQUAL(1000, notes[2].duration_ms);
    TEST_ASSERT_EQUAL(188, notes[3].duration_ms);
    TEST_ASSERT_EQUAL(2000, notes[4].duration_ms);
    TEST_ASSERT_EQUAL(3938, espos_rtttl_duration_ms(notes, n));
    TEST_ASSERT_EQUAL(0, espos_rtttl_duration_ms(notes, 0));
}

/* -------------------------------------------------------------- defaults */

TEST_CASE("rtttl: an empty defaults section uses d=4, o=5, b=63", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    /* Quarter at 63 bpm = 240000 / 63 / 4 = 952.4 -> 952 ms. */
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x::c", notes, MAX));
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);
    TEST_ASSERT_EQUAL(952, notes[0].duration_ms);
}

TEST_CASE("rtttl: a default that is left out keeps RTTTL's own", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    /* Only the tempo given: still a quarter, still octave 5. */
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x:b=120:c", notes, MAX));
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);
    TEST_ASSERT_EQUAL(500, notes[0].duration_ms);
}

TEST_CASE("rtttl: a string with no ':' is all notes, with the defaults", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    TEST_ASSERT_EQUAL(2, espos_rtttl_parse("c,e", notes, MAX));
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);
    TEST_ASSERT_EQUAL(952, notes[0].duration_ms);
}

TEST_CASE("rtttl: a malformed header falls back per key, not wholesale", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    /* d has no value, o is not a number, junk keys and stray commas around
     * them -- b=120 is the only setting that reads, and it still applies. */
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x:d=,o=zz,,q=7,b=120,nonsense:c", notes, MAX));
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);
    TEST_ASSERT_EQUAL(500, notes[0].duration_ms);
}

TEST_CASE("rtttl: a default written without '=' does not take the next key's value", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    /* "o5" has no '='; the search for one used to run on into "b=120" and
     * read it as the octave (clamped to 8: 4186 Hz), leaving the tempo at 63. */
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x:o5,b=120:c", notes, MAX));
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);     /* octave stays 5 */
    TEST_ASSERT_EQUAL(500, notes[0].duration_ms); /* b=120 still applies */
    /* Same with a bare "d": it used to take o=6 as the duration. */
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x:d,o=6,b=120:c", notes, MAX));
    TEST_ASSERT_EQUAL(1047, notes[0].freq_hz);    /* o=6 applies */
    TEST_ASSERT_EQUAL(500, notes[0].duration_ms); /* d stays 4 */
    /* A bare key last in the section. */
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x:b=120,o:c", notes, MAX));
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);
    TEST_ASSERT_EQUAL(500, notes[0].duration_ms);
}

TEST_CASE("rtttl: a zero tempo or zero default duration plays nothing", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    TEST_ASSERT_EQUAL(0, espos_rtttl_parse("x:b=0:c,e", notes, MAX));
    TEST_ASSERT_EQUAL(0, espos_rtttl_parse("x:d=0:c,e", notes, MAX));
}

TEST_CASE("rtttl: keys and notes are case-insensitive, spaces are ignored", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("X:D=8,O=6,B=120:A", notes, MAX));
    TEST_ASSERT_EQUAL(1760, notes[0].freq_hz);
    TEST_ASSERT_EQUAL(250, notes[0].duration_ms);
    TEST_ASSERT_EQUAL(2, espos_rtttl_parse("x: d=4, o=5, b=120 : c , e ", notes, MAX));
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);
    TEST_ASSERT_EQUAL(659, notes[1].freq_hz);
}

/* ------------------------------------------------ sharps, dots and rests */

TEST_CASE("rtttl: a sharp raises the note a semitone, a dot adds half its length", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    /* Whole note at 60 bpm = 4000 ms; quarter = 1000 ms; dotted = 1500 ms. */
    size_t n = espos_rtttl_parse("x:d=4,o=5,b=60:4c#6.", notes, MAX);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL(1109, notes[0].freq_hz); /* c#6 */
    TEST_ASSERT_EQUAL(1500, notes[0].duration_ms);
}

TEST_CASE("rtttl: the dot may come before or after the octave", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    TEST_ASSERT_EQUAL(2, espos_rtttl_parse("x:d=4,o=5,b=60:c.6,c6.", notes, MAX));
    TEST_ASSERT_EQUAL(1047, notes[0].freq_hz);
    TEST_ASSERT_EQUAL(1500, notes[0].duration_ms);
    TEST_ASSERT_EQUAL(1047, notes[1].freq_hz);
    TEST_ASSERT_EQUAL(1500, notes[1].duration_ms);
}

TEST_CASE("rtttl: b# rolls over into the next octave's c", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x:d=4,o=5,b=60:b#", notes, MAX));
    TEST_ASSERT_EQUAL(1047, notes[0].freq_hz); /* c6 */
}

TEST_CASE("rtttl: 'p' is a rest -- no frequency, no tone, but it takes time", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    size_t n = espos_rtttl_parse("x:d=4,o=5,b=120:c,p,e", notes, MAX);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL(0, notes[1].freq_hz);
    TEST_ASSERT_EQUAL(500, notes[1].duration_ms);
    uint16_t f = 42;
    TEST_ASSERT_FALSE(espos_rtttl_tone_at(notes, n, notes[0].duration_ms, &f)); /* resting */
    TEST_ASSERT_EQUAL(42, f);                                                   /* untouched */
    TEST_ASSERT_EQUAL(1500, espos_rtttl_duration_ms(notes, n));
}

TEST_CASE("rtttl: a tune of only rests parses, and never sounds", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    size_t n = espos_rtttl_parse("x:d=4,o=5,b=120:p,2p", notes, MAX);
    TEST_ASSERT_EQUAL(2, n);
    uint16_t f;
    for (uint32_t t = 0; t < 1500; t += 50) {
        TEST_ASSERT_FALSE(espos_rtttl_tone_at(notes, n, t, &f));
    }
}

/* ---------------------------------------------------------- bad input */

TEST_CASE("rtttl: unrecognised tokens are skipped, not fatal", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    size_t n = espos_rtttl_parse("x:d=4,o=5,b=120:c,q,8,#,e", notes, MAX);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL(523, notes[0].freq_hz);
    TEST_ASSERT_EQUAL(659, notes[1].freq_hz);
}

TEST_CASE("rtttl: empty or note-less input parses to zero notes", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    TEST_ASSERT_EQUAL(0, espos_rtttl_parse("", notes, MAX));
    TEST_ASSERT_EQUAL(0, espos_rtttl_parse(NULL, notes, MAX));
    TEST_ASSERT_EQUAL(0, espos_rtttl_parse("x:d=4,o=5,b=120:", notes, MAX));
    TEST_ASSERT_EQUAL(0, espos_rtttl_parse("x:d=4,o=5,b=120", notes, MAX));
    TEST_ASSERT_EQUAL(0, espos_rtttl_parse("nonsense with no notes", notes, MAX));
    TEST_ASSERT_EQUAL(0, espos_rtttl_parse(",,, ,", notes, MAX));
}

TEST_CASE("rtttl: an out-of-range octave plays at octave 8, at once", "[rtttl]")
{
    /* A huge octave used to loop once per octave (minutes on the chip for
     * o=2147483647) and overflow the frequency; long digit strings
     * overflowed an int. */
    espos_rtttl_note_t notes[MAX];
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x:d=4,o=2147483647,b=63:c", notes, MAX));
    TEST_ASSERT_EQUAL(4186, notes[0].freq_hz); /* c8 */
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x:d=4,o=5,b=63:c99999999999999999999", notes, MAX));
    TEST_ASSERT_EQUAL(4186, notes[0].freq_hz);
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x:d=4,o=5,b=63:b#8", notes, MAX));
    TEST_ASSERT_EQUAL(8372, notes[0].freq_hz); /* b#8 is c9, still in range */
}

TEST_CASE("rtttl: octave 0 is in range, not clamped", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x:d=4,o=5,b=63:a0", notes, MAX));
    TEST_ASSERT_EQUAL(28, notes[0].freq_hz); /* a0 = 27.5 Hz */
}

TEST_CASE("rtttl: huge numbers are capped, not wrapped", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    /* A tempo that overflowed an int used to wrap negative and drop the tune. */
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x:d=1,o=5,b=99999999999:c", notes, MAX));
    TEST_ASSERT_EQUAL(24, notes[0].duration_ms); /* 240000 / 9999 */
    /* A note longer than uint16_t holds is cut to its maximum, not wrapped. */
    TEST_ASSERT_EQUAL(1, espos_rtttl_parse("x:d=1,o=5,b=1:c.", notes, MAX));
    TEST_ASSERT_EQUAL(65535, notes[0].duration_ms);
}

/* ------------------------------------------------------- note capacity */

TEST_CASE("rtttl: a tune longer than ESPOS_RTTTL_MAX_NOTES stops at the cap", "[rtttl]")
{
    /* 70 notes, then a sentinel past the array: nothing beyond MAX is
     * written, and the notes kept are the first ones. */
    char tune[8 + 70 * 2 + 1] = "x::";
    for (int i = 0; i < 70; i++) {
        strcat(tune, i ? ",c" : "e");
    }
    struct {
        espos_rtttl_note_t notes[MAX];
        espos_rtttl_note_t guard;
    } buf;
    memset(&buf.guard, 0xA5, sizeof(buf.guard));
    TEST_ASSERT_EQUAL(MAX, espos_rtttl_parse(tune, buf.notes, MAX));
    TEST_ASSERT_EQUAL(659, buf.notes[0].freq_hz);
    TEST_ASSERT_EQUAL(523, buf.notes[MAX - 1].freq_hz);
    TEST_ASSERT_EQUAL_HEX16(0xA5A5, buf.guard.freq_hz);
    TEST_ASSERT_EQUAL_HEX16(0xA5A5, buf.guard.duration_ms);
}

TEST_CASE("rtttl: a smaller max is honoured, and max 0 writes nothing", "[rtttl]")
{
    espos_rtttl_note_t notes[3];
    memset(notes, 0, sizeof(notes));
    TEST_ASSERT_EQUAL(2, espos_rtttl_parse("x::c,d,e,f", notes, 2));
    TEST_ASSERT_EQUAL(0, notes[2].freq_hz);
    TEST_ASSERT_EQUAL(0, espos_rtttl_parse("x::c,d,e,f", notes, 0));
}

/* ------------------------------------------------------------ tone_at */

TEST_CASE("rtttl: the tone follows the notes, then stops (one pass, no loop)", "[rtttl]")
{
    espos_rtttl_note_t notes[MAX];
    size_t n = espos_rtttl_parse("boot:d=16,o=5,b=200:c,e,g", notes, MAX);
    uint16_t f = 0;
    TEST_ASSERT_TRUE(espos_rtttl_tone_at(notes, n, 0, &f));
    TEST_ASSERT_EQUAL(notes[0].freq_hz, f);
    TEST_ASSERT_TRUE(espos_rtttl_tone_at(notes, n, 74, &f));
    TEST_ASSERT_EQUAL(notes[0].freq_hz, f);
    TEST_ASSERT_TRUE(espos_rtttl_tone_at(notes, n, 75, &f));
    TEST_ASSERT_EQUAL(notes[1].freq_hz, f);
    const uint32_t total = espos_rtttl_duration_ms(notes, n);
    TEST_ASSERT_TRUE(espos_rtttl_tone_at(notes, n, total - 1, &f));
    TEST_ASSERT_EQUAL(notes[2].freq_hz, f);
    TEST_ASSERT_FALSE(espos_rtttl_tone_at(notes, n, total, &f)); /* past the end */
    TEST_ASSERT_FALSE(espos_rtttl_tone_at(notes, n, UINT32_MAX, &f));
    TEST_ASSERT_FALSE(espos_rtttl_tone_at(notes, 0, 0, &f));
}
