# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# The one espOS example that does NOT use the prologue, because it is the one
# that has to work outside this repository.
#
# Every other example includes ../../../../cmake/espos_project.cmake -- the
# shared prologue, which is the right thing in-tree: it sets the sdkconfig
# defaults, the partition table, the IDF version policy and the component
# search path in one call. But that path exists only inside a checkout. Copy
# such an example anywhere else, or download it from the component registry,
# and it fails on the include before CMake reaches anything else.
#
# So this one is plain IDF plus a component dependency, exactly like every
# other registry component's example. What the prologue would have set is in
# sdkconfig.defaults instead, where espos_core's own configure-time lint can
# check it -- run the build without those lines and the lint prints the list.
cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(from_registry)

# The web UI bundle -> the LittleFS "storage" partition, written by
# `idf.py flash`. Defined by espos_httpd's project_include.cmake, so it is
# available without the prologue; it has to be called after project(),
# because littlefs_create_partition_image() comes from a managed component.
espos_project_ui_partition()
