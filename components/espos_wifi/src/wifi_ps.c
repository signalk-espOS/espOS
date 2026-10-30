// SPDX-FileCopyrightText: 2026 Dirk Wahrheit
// SPDX-License-Identifier: Apache-2.0
#include "wifi_ps.h"

#include <string.h>

const char *espos_wifi_ps_effective(const char *configured, bool i154_coex)
{
    if (configured && (strcmp(configured, "min") == 0 || strcmp(configured, "max") == 0)) {
        return configured;
    }
    return i154_coex ? "min" : (configured ? configured : "none");
}
