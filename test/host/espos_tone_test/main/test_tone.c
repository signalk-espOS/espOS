/* SPDX-FileCopyrightText: 2026 humppafreak
 * SPDX-License-Identifier: Apache-2.0
 *
 * espos_tone: the named tone library and the one-shot player.
 *
 * The library is a table setting the owner edits, so a bad row must cost that
 * row only and a broken table must cost nothing but the chirps. The player is
 * driven by a caller's clock: it must play each note at its pitch, stay quiet
 * in rests, stop by itself after one pass and not care that the clock wraps.
 */
#include <string.h>

#include "espos_tone.h"
#include "unity.h"

/* -------------------------------------------------------------- library */

TEST_CASE("tone library: valid rows parse, invalid rows are skipped", "[tone]")
{
    const char *json = "[{\"name\":\"boot\",\"rtttl\":\"boot:d=16,o=5,b=200:c,e,g\"},"
                       "{\"name\":\"unplayable\",\"rtttl\":\"nonsense with no notes\"},"
                       "{\"name\":\"\",\"rtttl\":\"x:d=4,o=5,b=60:c\"},"
                       "{\"rtttl\":\"x:d=4,o=5,b=60:c\"},"
                       "{\"name\":\"no-tune\"},"
                       "{\"name\":\"number\",\"rtttl\":42},"
                       "{\"name\":7,\"rtttl\":\"x:d=4,o=5,b=60:c\"},"
                       "\"not an object\","
                       "{\"name\":\"relay-on\",\"rtttl\":\"relay-on:d=16,o=6,b=250:c\"}]";
    espos_tone_t tones[ESPOS_TONE_MAX_TONES];
    size_t n = espos_tone_library_parse(json, tones, ESPOS_TONE_MAX_TONES);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("boot", tones[0].name);
    TEST_ASSERT_EQUAL(3, tones[0].n_notes);
    TEST_ASSERT_EQUAL(523, tones[0].notes[0].freq_hz);
    TEST_ASSERT_EQUAL_STRING("relay-on", tones[1].name);
    TEST_ASSERT_EQUAL(1, tones[1].n_notes);
}

TEST_CASE("tone library: a row whose notes all round to 0 ms is skipped", "[tone]")
{
    /* 240000 / 9999 / 9999 is well under half a millisecond: the tune parses,
     * but the player would start and never sound. */
    const char *json = "[{\"name\":\"silent\",\"rtttl\":\"x:d=9999,o=5,b=9999:c,e\"},"
                       "{\"name\":\"boot\",\"rtttl\":\"x::c\"}]";
    espos_tone_t tones[2];
    TEST_ASSERT_EQUAL(1, espos_tone_library_parse(json, tones, 2));
    TEST_ASSERT_EQUAL_STRING("boot", tones[0].name);
}

TEST_CASE("tone library: a name of exactly ESPOS_TONE_NAME_MAX fits, one more is skipped", "[tone]")
{
    char json[256];
    char name[ESPOS_TONE_NAME_MAX + 2];
    memset(name, 'a', ESPOS_TONE_NAME_MAX);
    name[ESPOS_TONE_NAME_MAX] = '\0';
    snprintf(json, sizeof(json), "[{\"name\":\"%s\",\"rtttl\":\"x::c\"},{\"name\":\"%sb\",\"rtttl\":\"x::c\"}]", name, name);
    espos_tone_t tones[2];
    TEST_ASSERT_EQUAL(1, espos_tone_library_parse(json, tones, 2));
    TEST_ASSERT_EQUAL_STRING(name, tones[0].name);
}

TEST_CASE("tone library: malformed JSON, a non-array or an empty array is zero tones", "[tone]")
{
    espos_tone_t tones[ESPOS_TONE_MAX_TONES];
    TEST_ASSERT_EQUAL(0, espos_tone_library_parse("not json", tones, ESPOS_TONE_MAX_TONES));
    TEST_ASSERT_EQUAL(0, espos_tone_library_parse(NULL, tones, ESPOS_TONE_MAX_TONES));
    TEST_ASSERT_EQUAL(0, espos_tone_library_parse("", tones, ESPOS_TONE_MAX_TONES));
    TEST_ASSERT_EQUAL(0, espos_tone_library_parse("[]", tones, ESPOS_TONE_MAX_TONES));
    TEST_ASSERT_EQUAL(0, espos_tone_library_parse("{\"name\":\"boot\",\"rtttl\":\"x::c\"}", tones, ESPOS_TONE_MAX_TONES));
    TEST_ASSERT_EQUAL(0, espos_tone_library_parse("[{\"name\":\"boot\",\"rtttl\":\"x::c\"}", tones, ESPOS_TONE_MAX_TONES));
}

TEST_CASE("tone library: at most max rows are written", "[tone]")
{
    const char *json = "[{\"name\":\"a\",\"rtttl\":\"x::c\"},"
                       "{\"name\":\"b\",\"rtttl\":\"x::d\"},"
                       "{\"name\":\"c\",\"rtttl\":\"x::e\"}]";
    espos_tone_t tones[3];
    memset(tones, 0, sizeof(tones));
    TEST_ASSERT_EQUAL(2, espos_tone_library_parse(json, tones, 2));
    TEST_ASSERT_EQUAL_STRING("a", tones[0].name);
    TEST_ASSERT_EQUAL_STRING("b", tones[1].name);
    TEST_ASSERT_EQUAL_STRING("", tones[2].name);
    TEST_ASSERT_EQUAL(0, espos_tone_library_parse(json, tones, 0));
}

TEST_CASE("tone library: lookup by name, empty name and unknown name", "[tone]")
{
    espos_tone_t tones[2] = { 0 };
    strcpy(tones[0].name, "boot");
    strcpy(tones[1].name, "relay-on");
    TEST_ASSERT_EQUAL(0, espos_tone_library_find(tones, 2, "boot"));
    TEST_ASSERT_EQUAL(1, espos_tone_library_find(tones, 2, "relay-on"));
    TEST_ASSERT_EQUAL(-1, espos_tone_library_find(tones, 2, ""));
    TEST_ASSERT_EQUAL(-1, espos_tone_library_find(tones, 2, NULL));
    TEST_ASSERT_EQUAL(-1, espos_tone_library_find(tones, 2, "nope"));
    TEST_ASSERT_EQUAL(-1, espos_tone_library_find(tones, 2, "Boot")); /* case-sensitive */
    TEST_ASSERT_EQUAL(-1, espos_tone_library_find(tones, 0, "boot"));
}

TEST_CASE("tone library: a duplicate name finds the first row", "[tone]")
{
    const char *json = "[{\"name\":\"boot\",\"rtttl\":\"x::c\"},{\"name\":\"boot\",\"rtttl\":\"x::e\"}]";
    espos_tone_t tones[2];
    TEST_ASSERT_EQUAL(2, espos_tone_library_parse(json, tones, 2));
    int i = espos_tone_library_find(tones, 2, "boot");
    TEST_ASSERT_EQUAL(0, i);
    TEST_ASSERT_EQUAL(523, tones[i].notes[0].freq_hz);
}

/* --------------------------------------------------------------- player */

static espos_tone_t three_notes(void)
{
    /* 8th notes at 120 bpm = 250 ms each: a6 (1760 Hz), a rest, then c7. */
    espos_tone_t t = { 0 };
    t.n_notes = espos_rtttl_parse("x:d=8,o=6,b=120:a,p,c7", t.notes, ESPOS_RTTTL_MAX_NOTES);
    return t;
}

TEST_CASE("player: sounds each note at its pitch, silent in a rest", "[tone]")
{
    espos_tone_t t = three_notes();
    espos_tone_player_t p = { 0 };
    uint16_t hz = 0;
    TEST_ASSERT_TRUE(espos_tone_player_start(&p, &t, 1000));
    TEST_ASSERT_TRUE(espos_tone_player_step(&p, 1000, &hz));
    TEST_ASSERT_EQUAL(1760, hz);
    hz = 42;
    TEST_ASSERT_FALSE(espos_tone_player_step(&p, 1300, &hz)); /* the rest */
    TEST_ASSERT_EQUAL(42, hz);                                /* pitch left alone */
    TEST_ASSERT_TRUE(p.playing);                              /* a rest is not the end */
    TEST_ASSERT_TRUE(espos_tone_player_step(&p, 1600, &hz));
    TEST_ASSERT_EQUAL(2093, hz);
}

TEST_CASE("player: one pass, then it stops by itself", "[tone]")
{
    espos_tone_t t = three_notes();
    espos_tone_player_t p = { 0 };
    uint16_t hz = 0;
    espos_tone_player_start(&p, &t, 0);
    TEST_ASSERT_EQUAL(750, p.len_ms);
    TEST_ASSERT_TRUE(espos_tone_player_step(&p, 749, &hz));
    TEST_ASSERT_FALSE(espos_tone_player_step(&p, 750, &hz));
    TEST_ASSERT_FALSE(p.playing);
    TEST_ASSERT_FALSE(espos_tone_player_step(&p, 0, &hz)); /* not restarted by an earlier time */
}

TEST_CASE("player: an empty tone doesn't start; stop is immediate", "[tone]")
{
    espos_tone_t empty = { 0 };
    espos_tone_t t = three_notes();
    espos_tone_player_t p = { 0 };
    uint16_t hz = 0;
    TEST_ASSERT_FALSE(espos_tone_player_start(&p, &empty, 0));
    TEST_ASSERT_FALSE(p.playing);
    espos_tone_player_start(&p, &t, 0);
    espos_tone_player_stop(&p);
    TEST_ASSERT_FALSE(espos_tone_player_step(&p, 10, &hz));
    espos_tone_player_stop(&p); /* idle: a no-op */
    TEST_ASSERT_FALSE(p.playing);
}

TEST_CASE("player: an empty tone also stops whatever was playing", "[tone]")
{
    espos_tone_t empty = { 0 };
    espos_tone_t t = three_notes();
    espos_tone_player_t p = { 0 };
    uint16_t hz = 0;
    espos_tone_player_start(&p, &t, 0);
    TEST_ASSERT_FALSE(espos_tone_player_start(&p, &empty, 10));
    TEST_ASSERT_FALSE(espos_tone_player_step(&p, 20, &hz));
}

TEST_CASE("player: starting again replaces the tune and restarts the clock", "[tone]")
{
    espos_tone_t t = three_notes();
    espos_tone_t other = { 0 };
    other.n_notes = espos_rtttl_parse("y:d=4,o=5,b=120:e", other.notes, ESPOS_RTTTL_MAX_NOTES);
    espos_tone_player_t p = { 0 };
    uint16_t hz = 0;
    espos_tone_player_start(&p, &t, 0);
    espos_tone_player_start(&p, &other, 600);
    TEST_ASSERT_TRUE(espos_tone_player_step(&p, 600, &hz));
    TEST_ASSERT_EQUAL(659, hz);
    TEST_ASSERT_TRUE(espos_tone_player_step(&p, 1099, &hz));
    TEST_ASSERT_FALSE(espos_tone_player_step(&p, 1100, &hz));
}

TEST_CASE("player: plays its own copy, so the library can be reparsed meanwhile", "[tone]")
{
    espos_tone_t t = three_notes();
    espos_tone_player_t p = { 0 };
    uint16_t hz = 0;
    espos_tone_player_start(&p, &t, 0);
    memset(&t, 0, sizeof(t));
    TEST_ASSERT_TRUE(espos_tone_player_step(&p, 0, &hz));
    TEST_ASSERT_EQUAL(1760, hz);
}

TEST_CASE("player: survives the millisecond clock wrapping", "[tone]")
{
    espos_tone_t t = three_notes();
    espos_tone_player_t p = { 0 };
    uint16_t hz = 0;
    espos_tone_player_start(&p, &t, UINT32_MAX - 100);
    TEST_ASSERT_TRUE(espos_tone_player_step(&p, 50, &hz)); /* 151 ms in: first note */
    TEST_ASSERT_EQUAL(1760, hz);
    TEST_ASSERT_TRUE(espos_tone_player_step(&p, 500, &hz)); /* 601 ms in: third note */
    TEST_ASSERT_EQUAL(2093, hz);
    TEST_ASSERT_FALSE(espos_tone_player_step(&p, 649, &hz)); /* 750 ms in: done */
    TEST_ASSERT_FALSE(p.playing);
}
