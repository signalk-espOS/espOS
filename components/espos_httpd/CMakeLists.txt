# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
idf_build_get_property(target IDF_TARGET)
set(priv_reqs esp_timer esp_app_format esp_system esp_event espos_log espos_event espos_health espressif__cjson)
if(NOT target STREQUAL "linux")
    # spi_flash/esp_hw_support/heap: the hardware block in api_system.c
    # (flash size, base MAC, RAM totals). Device-only, like the rest of this
    # list -- the linux target has none of them and the code is #if-guarded.
    list(APPEND priv_reqs esp_netif espcoredump esp_partition joltwallet__littlefs
                          spi_flash esp_hw_support heap)
endif()
idf_component_register(
    SRCS src/espos_httpd.c src/api_config.c src/api_system.c src/api_logs.c src/api_coredump.c src/static.c src/sse.c
         src/auth.c src/auth_policy.c
    INCLUDE_DIRS include
    REQUIRES esp_http_server espos_config
    PRIV_REQUIRES ${priv_reqs}
    EMBED_TXTFILES www/index.html)

espos_config_add_descriptor(config/httpd.json)
