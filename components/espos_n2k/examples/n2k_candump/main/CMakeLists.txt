# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# espos_core is the boot; espos_n2k is the gateway; espos_ota is over-the-air
# updates from the web UI. There is no espos_sk here on purpose: this device
# publishes no SignalK paths of its own, it streams candump and lets the
# server decode -- but espos_start() still brings SignalK up for the web UI's
# status page if the component is in the build.
idf_component_register(SRCS main.cpp PRIV_REQUIRES espos_core espos_n2k espos_ota)
