# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0

idf_component_register(
    SRCS "src/espos_prov.c" "src/api_prov.c"
    INCLUDE_DIRS "include"
    REQUIRES espos_config
    PRIV_REQUIRES protocomm esp_wifi
)

# Optional at link time: provisioning works without either, but when they are
# present it serves GET /api/v1/prov and suspends the BLE scanner for the
# duration. The sources test __has_include, so a build without them compiles
# the same file with those paths removed.
# Namespace-tolerant component checks (espOS #138). Its own copy, because a
# component installed from the registry cannot reach a sibling's directory,
# and this component does not require espos_core. The file guards against
# being included twice.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/espos_components.cmake")

espos_optional_requires(PRIVATE espos_httpd espos_ble)

# The PoP lives here: generated at random on first use and kept, so the person
# standing at the device can read it once and use it again after a reboot.
espos_config_add_descriptor(config/prov.json)
