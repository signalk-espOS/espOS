# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# espos_core is the boot and starts provisioning on its own when no network is
# configured; espos_prov is named because main.c reads its state to narrate
# what is happening, and because MINIMAL_BUILD compiles only what is required.
# espos_sk is SignalK, what the device does once it has the credentials --
# which is the point of provisioning it. espos_ota is over-the-air updates.
idf_component_register(SRCS main.c PRIV_REQUIRES espos_core espos_prov espos_sk espos_ota)
