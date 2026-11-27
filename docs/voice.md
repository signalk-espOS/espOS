# Voice satellite

`espos_voice` is a [Wyoming-protocol](https://github.com/rhasspy/wyoming)
satellite: a TCP server (default port 10700) that lets a
signalk-wyoming or Home Assistant orchestrator play TTS to the device
and stream microphone audio back, with on-device wake-word detection via
esp-sr WakeNet.

espOS supplies the protocol, the server and the wake engine. It does not
supply an audio codec: the application implements
`espos_audio::AudioDriver` for whatever the board has and passes it in.

```cpp
#include "espos_audio/audio_driver.h"
#include "espos_voice/wyoming_satellite.h"

static MyBoardAudio audio;          // : public espos_audio::AudioDriver
static espos_voice::WyomingSatellite sat(&audio, cfg);
audio.init();
sat.start();
```

The satellite holds up to 3 sockets: its listener, the orchestrator's
connection, and the connection to a network wake service when the wake
word is detected on the server. It serves one orchestrator at a time: a
second one waits in the listen queue, with a TCP connection but no socket,
until the first disconnects. espOS's configure check counts the 3 sockets
in `CONFIG_LWIP_MAX_SOCKETS` and the 3 connections (both orchestrators and
the wake service) in `CONFIG_LWIP_MAX_ACTIVE_TCP` on the two targets the
satellite runs on, and espos.defaults has room for them.

## espos_audio

A header-only contract, deliberately minimal: `init()`, `ready()`,
`sample_rate()`, `play_pcm()`, plus optional capture and mic-probe hooks.
`espos_audio::NullAudio` is a no-op implementation for boards with no
audio, so callers never need a null check.

`play_pcm()` is non-blocking by contract — it copies and enqueues — so it
is safe to call from a UI or render task. A driver that blocks there will
stall the display.

## Targets

esp-sr supports only `esp32s3` and `esp32p4`. On every other target
`espos_voice` registers as an empty component rather than failing the
build, so a project can depend on it unconditionally and simply have no
voice where the silicon cannot run WakeNet.

## Wake word

The satellite owns a `WakeEngine` directly and listens with it unless
`config.on_device_wake` is false; `set_wake_network()` moves detection to
a network wake service (openWakeWord) at runtime. Detection pauses the
engine for the duration of an utterance and re-arms it afterwards; see
the note in `wake_engine.cpp` about restoring the shared I2S capture
clock after playback, which is what stops WakeNet going deaf after the
first TTS reply.

`set_wake_enabled(false)` switches the wake word off and leaves the
satellite on push-to-talk: the engine is deleted, which gives the mic and
WakeNet's memory back, and a network wake task disconnects from its
service. `set_wake_enabled(true)` brings back whichever of the two the
satellite was set up for. Called before `start()`, it only records the
choice, so a device that should not listen never loads WakeNet:

```cpp
if (sat.set_wake_enabled(false)) {  // push-to-talk only
  sat.start();
}
```

Before `start()` a false means the choice was not recorded. Either
another task was switching the satellite at that moment, and a later
try can succeed, or the satellite could not create its lifecycle lock
when it was constructed (out of memory), and every call fails. Check
each try, and start the satellite only once one returns true.

While the satellite runs, `false` from `set_wake_enabled()` means the
switch did not complete: a network wake task that is still in an
utterance gets 8 s to exit, and one that has not may still be connected.
Calling it again tries the join again.

To switch voice off entirely, `stop()` the satellite: its port closes, and
the orchestrator sees the device go away. `start()` brings it back.
