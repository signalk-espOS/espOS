# espos_voice

Wyoming voice satellite with esp-sr wake word.

```sh
idf.py add-dependency "signalk-espos/espos_voice^0.7"
```

Part of [espOS](https://github.com/signalk-espOS/espOS), an ESP-IDF runtime for Signal K devices. The components are released in lockstep, so one version of `espos_voice` works with the same version of every other.

**Documentation:** [docs/voice.md](https://github.com/signalk-espOS/espOS/blob/main/docs/voice.md)

Built on the espOS tree as a git submodule, or installed from the registry; either way `REQUIRES espos_voice` in a component's `CMakeLists.txt` resolves it.

Apache-2.0. Issues and pull requests: [https://github.com/signalk-espOS/espOS/issues](https://github.com/signalk-espOS/espOS/issues).
