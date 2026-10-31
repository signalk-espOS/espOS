/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 */
/* Its own file so the host test can build it with known identity macros,
 * without espos_net.c's netif and config dependencies. */
#include "espos_net.h"
#include "net_identity.h"

const char *espos_net_user_agent(void)
{
    return "espOS/" ESPOS_MDNS_ESPOS_VERSION " " ESPOS_MDNS_PROJECT_NAME "/" ESPOS_MDNS_PROJECT_VER;
}
