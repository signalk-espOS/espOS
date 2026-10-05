# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# espos_core is the boot, espos_n2k the bus and the node, espos_ota updates
# from the web UI.
idf_component_register(SRCS main.cpp
                       PRIV_REQUIRES espos_core espos_n2k espos_ota esp_driver_gpio)
