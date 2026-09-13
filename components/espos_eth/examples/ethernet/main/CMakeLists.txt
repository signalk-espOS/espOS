# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# espos_core is the boot; espos_eth the cable; espos_net is named because
# main.c reads the route from it; espos_sk is SignalK (it publishes the device's
# own health on its own); espos_ota is over-the-air updates from the web UI.
idf_component_register(SRCS main.c PRIV_REQUIRES espos_core espos_eth espos_net espos_sk espos_ota)
