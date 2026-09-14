# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# espos_core is the boot and espos_power the cycle; espos_sk publishes the
# readings and is what the cycle flushes before sleeping; espos_ota keeps a
# sleeping device updatable during its awake window. espos_eth is named so
# MINIMAL_BUILD compiles it: the P4 PoE board's only network with WiFi off.
idf_component_register(SRCS main.c PRIV_REQUIRES espos_core espos_power espos_sk espos_ota espos_eth)
