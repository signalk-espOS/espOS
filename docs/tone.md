# Event tones

A device with a buzzer usually wants more than an alarm: a short chirp when it
boots, when a relay switches, when a button is pressed. `espos_tone` gives it
those as RTTTL tunes the owner can edit, looked up by name, and a player that
works out what to sound when. It does not drive the buzzer itself. The board
does that, with the LEDC timer and channel it already owns.

```c
#include "espos_tone.h"

static espos_tone_t s_tones[ESPOS_TONE_MAX_TONES];
static size_t s_n_tones;
static espos_tone_player_t s_player;

/* On boot and whenever the table setting changes. */
s_n_tones = espos_tone_library_parse(json, s_tones, ESPOS_TONE_MAX_TONES);

/* On an event. */
int i = espos_tone_library_find(s_tones, s_n_tones, "relay-on");
if (i >= 0) {
    espos_tone_player_start(&s_player, &s_tones[i], now_ms());
}

/* Every few milliseconds, from the task that already runs the LEDs. */
uint16_t hz;
if (espos_tone_player_step(&s_player, now_ms(), &hz)) {
    ledc_set_freq(LEDC_LOW_SPEED_MODE, BUZZER_TIMER, hz);
    buzzer_on();
} else {
    buzzer_off();
}
```

A firmware adds `espos_tone` to the `REQUIRES` of its own component (main's
`CMakeLists.txt`, or whichever component drives the buzzer). It is a core
component, so it does not go in `espos_project_prologue(... COMPONENTS ...)`.

`now_ms()` is any free-running millisecond clock, such as
`esp_timer_get_time() / 1000` cut to `uint32_t`. The player is wrap-safe, so
the clock may roll over in the middle of a tune.

## RTTTL

A tune is one string in RTTTL (Ring Tone Text Transfer Language), the format
old phones used for ringtones:

```
boot:d=16,o=5,b=200:g,a,b,c6,d6,e6,f#6,g6,a6,b6,2c7
```

It has three parts separated by colons:

* a **name**, which the parser ignores (the library row has its own);
* the **defaults**: `d=` note length (4 is a quarter note), `o=` octave and
  `b=` tempo in beats per minute. Anything left out keeps RTTTL's own
  defaults of `d=4,o=5,b=63`;
* the **notes**, comma-separated. A note is an optional length, a letter
  `a`–`g` (or `p` for a rest), an optional `#`, an optional octave and an
  optional `.`, which makes it half as long again. `8c#6.` is a dotted
  eighth-note C sharp in octave 6.

A string with no colon at all is read as notes with the defaults, so `c,e,g`
is a valid tune.

`espos_rtttl_parse()` reads at most `ESPOS_RTTTL_MAX_NOTES` (64) notes and is
lenient on purpose, because a tune is something a person types into a
settings page. A token it does not recognise costs that one note, not the
tune. A bad default falls back to RTTTL's own value for that key only. Numbers
are capped rather than wrapped, and octaves above 8 play at octave 8 (c8 is
about 4.2 kHz, already more than a piezo buzzer reproduces well). A zero tempo
or zero default length gives no notes at all. The parser is host-tested and
fuzzed (`test/fuzz/run.sh rtttl`).

`espos_rtttl.h` can be used on its own when one fixed tune is all a device
needs:

```c
espos_rtttl_note_t notes[ESPOS_RTTTL_MAX_NOTES];
size_t n = espos_rtttl_parse("ok:d=16,o=6,b=200:c,e", notes, ESPOS_RTTTL_MAX_NOTES);
uint32_t len = espos_rtttl_duration_ms(notes, n);
uint16_t hz;
bool on = espos_rtttl_tone_at(notes, n, t_ms, &hz);
```

## The tone library

The tunes a device knows live in one [table setting](config.md) with the
columns `name` and `rtttl`. The owner edits it in the web UI, and each event
picks its tune by name in a setting of its own:

```json
{"name": "tones", "type": "string", "maxLength": 3999,
 "default": "[{\"name\":\"boot\",\"rtttl\":\"boot:d=16,o=5,b=200:c,e,g\"},{\"name\":\"relay-on\",\"rtttl\":\"relay-on:d=16,o=7,b=250:c\"}]",
 "x": {"format": "table", "columns": ["name", "rtttl"]}},
{"name": "boot_tone", "type": "string", "maxLength": 24, "default": "boot",
 "description": "Tone to play once boot finishes. Empty = no chirp."}
```

`espos_tone_library_parse()` turns the table into an array of
`espos_tone_t`. It skips a row whose name is missing, empty or longer than
`ESPOS_TONE_NAME_MAX` (24) characters, or whose tune has no playable notes
(including a tune so fast that every note rounds to 0 ms).
Malformed JSON gives zero tones rather than an error, so a broken table costs
the chirps and nothing else. `espos_tone_library_find()` looks a tone up by
name (case-sensitive, first match). An empty name finds nothing, which is
the natural way to say "no chirp for this event".

Each `espos_tone_t` holds its notes parsed, about 290 bytes, so a full
library of `ESPOS_TONE_MAX_TONES` (24) is about 7 KB. That is a suggested
size, not a limit: pass whatever array you have.

## The player

`espos_tone_player_t` plays one tone once. `espos_tone_player_start()` copies
the tone in, so the library can be reparsed while a tune plays, and replaces
anything already playing. `espos_tone_player_step()` says whether a note
sounds at `now_ms`, and at what pitch. It returns false during a rest and from
the end of the tune on, and at the end the player stops by itself.
`espos_tone_player_stop()` cuts a tune off.

Step it often enough for the shortest note you use. A 32nd note at 250 bpm is
30 ms, so a 10 ms tick is plenty.

## What stays in the firmware

Two things are deliberately left to the board:

* **The buzzer.** A passive buzzer needs a square wave whose frequency
  changes per note, which on an ESP32 means an LEDC timer and channel. Those
  belong to the firmware, which is the only code that knows what else it
  drives with them, so espOS hands back a frequency and the board calls
  `ledc_set_freq()`.
* **Which sound wins.** When an alarm, a status override and an event chirp
  all want the same buzzer, the order between them is a policy that depends
  on what the board has. Call `espos_tone_player_stop()` when something more
  important takes over, and skip `start()` while it lasts.
