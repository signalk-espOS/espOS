# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# espos_net: the interface-agnostic network seam — default route, hostname,
# device id, mDNS responder, /net/status. Transports (espos_wifi, later
# Ethernet) report into it; SignalK, OTA and mDNS depend on it instead of on
# a radio, so a firmware without WiFi still has all three.
idf_build_get_property(target IDF_TARGET)
set(srcs src/net_sm.c src/espos_net.c src/api_net.c src/mdns.c)
set(reqs espos_config espos_httpd)
set(priv_reqs espressif__cjson espos_event)
if(target STREQUAL "linux")
    # Host: no netif, a fixed MAC. espressif/mdns does not exist for this
    # target either; mdns.c compiles to stubs (CONFIG_ESPOS_NET_MDNS depends
    # on !IDF_TARGET_LINUX).
    list(APPEND srcs src/port_sim.c)
else()
    list(APPEND srcs src/port_idf.c)
    list(APPEND priv_reqs esp_netif esp_event esp_hw_support lwip)
    # The responder is a public requirement, not a private one: espos_mdns.h
    # is a thin queue in front of it, and the components that use it reach
    # for mdns.h themselves for what the wrapper does not cover (espos_sk
    # browses with mdns_query_ptr). REQUIRES hands them the include path;
    # linking alone (PRIV_REQUIRES) would not.
    list(APPEND reqs espressif__mdns)
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    REQUIRES ${reqs}
    PRIV_REQUIRES ${priv_reqs})

espos_config_add_descriptor(config/net.json)

# TXT records of _espos._tcp (src/mdns.c): the application's name and version
# the way esp_app_format reads them, and espOS's own version — version.txt two
# levels up in the tree, the manifest's version in a copy installed from the
# component registry. The same lookup espos_core makes for its banner,
# repeated here rather than shared: espos_net must not depend on espos_core,
# and a cmake/ helper would not travel with a registry copy of this component.
# See espos_core/CMakeLists.txt for why a checkout is recognised by
# cmake/espos_project.cmake, and not by version.txt two levels up alone.
get_filename_component(_espos_net_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(EXISTS "${_espos_net_root}/cmake/espos_project.cmake" AND EXISTS "${_espos_net_root}/version.txt")
    file(READ "${_espos_net_root}/version.txt" _espos_net_espos_version)
    # No CMAKE_CONFIGURE_DEPENDS on the file here: espos_core already adds
    # one, and the Ninja generator writes a phony edge per directory that
    # lists a file, so a second listing fails the generate step with
    # "version.txt is defined as an output multiple times".
else()
    idf_component_get_property(_espos_net_espos_version ${COMPONENT_NAME} COMPONENT_VERSION)
    if(NOT _espos_net_espos_version)
        file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/idf_component.yml" _espos_net_version_line
             REGEX "^version:" LIMIT_COUNT 1)
        string(REGEX REPLACE "^version:[ \t]*[\"']?([^\"' \t#]+).*$" "\\1"
               _espos_net_espos_version "${_espos_net_version_line}")
    endif()
endif()
string(STRIP "${_espos_net_espos_version}" _espos_net_espos_version)
if(NOT _espos_net_espos_version)
    set(_espos_net_espos_version "0.0.0-unknown")
endif()
idf_build_get_property(_espos_net_project_name PROJECT_NAME)
idf_build_get_property(_espos_net_project_ver PROJECT_VER)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESPOS_MDNS_ESPOS_VERSION="${_espos_net_espos_version}"
    ESPOS_MDNS_PROJECT_NAME="${_espos_net_project_name}"
    ESPOS_MDNS_PROJECT_VER="${_espos_net_project_ver}")
