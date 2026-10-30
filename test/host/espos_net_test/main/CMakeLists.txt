# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
# Compiles the machine's source directly: no netif, config or httpd needed.
set(net_src "${CMAKE_CURRENT_LIST_DIR}/../../../../components/espos_net")
idf_component_register(
    SRCS test_main.c test_net_sm.c test_user_agent.c
         "${net_src}/src/net_sm.c" "${net_src}/src/net_identity.c"
    INCLUDE_DIRS "." "${net_src}/include"
    PRIV_REQUIRES unity
    WHOLE_ARCHIVE)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESPOS_MDNS_ESPOS_VERSION="1.2.3"
    ESPOS_MDNS_PROJECT_NAME="demo-app"
    ESPOS_MDNS_PROJECT_VER="4.5.6")
