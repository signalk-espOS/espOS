# espos_tone

Short event tunes for a buzzer: an RTTTL parser, a named tone library read from a table setting, and a one-shot player that tells the board what pitch to sound. No hardware of its own; the board keeps its LEDC timer and channel.

```sh
idf.py add-dependency "signalk-espos/espos_tone^0.26"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_tone` works with the same version of every other.

**Documentation:** [docs/tone.md](https://github.com/signalk-espOS/espOS/blob/main/docs/tone.md)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_tone` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
