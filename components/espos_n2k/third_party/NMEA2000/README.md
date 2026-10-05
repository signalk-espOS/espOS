# NMEA2000 (vendored)

Timo Lappalainen's [NMEA2000](https://github.com/ttlappalainen/NMEA2000)
library, MIT (see `LICENSE`), at commit
`5b7b9fc3ccc18e30ebfba92da6486cffc6251595` (2025-12-18). It is the protocol
layer under `espos_n2k::Node`: address claim, ISO requests, product and
configuration information, PGN lists, heartbeat, group functions and
fast-packet assembly, plus the `N2kMessages.h` encoders and decoders an
application can use for the PGNs it sends.

## Why vendored

* A component published to the Espressif registry is downloaded on its own,
  so a git dependency is one more fetch every consumer's build needs, and
  upstream tags no releases to pin by.
* Upstream's `CMakeLists.txt` declares `cmake_minimum_required(VERSION 3.0)`,
  which CMake 4 refuses; consumers of the git component had to set
  `CMAKE_POLICY_VERSION_MINIMUM` in their own project to build it.
* The build (`components/espos_n2k/CMakeLists.txt`) compiles only the files
  here, and only with `CONFIG_ESPOS_N2K_NODE`.

## What is here

`src/` holds upstream's files unmodified: the core (`NMEA2000`, `N2kMsg`,
`N2kTimer`, `N2kStream`, `N2kDef`, `N2kTypes`, `N2kCANMsg`, the compiler
shims), the group-function handlers, `N2kMessages` and `N2kDeviceList`. Left
out: the Actisense, SeaSmart, CZone and Maretron extras, the per-board CAN
drivers (espos_n2k is the driver), examples and tests.

## Updating

Copy the same files from upstream's `src/` over these, update the commit
above, build an example with `CONFIG_ESPOS_N2K_NODE=y` and run the
`espos_n2k_test` host tests. Do not edit the files here: a fix goes upstream,
or into `src/node.cpp` around the library.
