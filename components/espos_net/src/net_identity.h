/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/* Set by CMakeLists.txt from what IDF knows about the application and from
 * espOS's version.txt (manifest version in a registry copy); the fallbacks
 * only exist so a build that lost the definitions still says something. */
#ifndef ESPOS_MDNS_PROJECT_NAME
#define ESPOS_MDNS_PROJECT_NAME "unknown"
#endif
#ifndef ESPOS_MDNS_PROJECT_VER
#define ESPOS_MDNS_PROJECT_VER "unknown"
#endif
#ifndef ESPOS_MDNS_ESPOS_VERSION
#define ESPOS_MDNS_ESPOS_VERSION "0.0.0-unknown"
#endif
