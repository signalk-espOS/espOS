# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
idf_component_register(
    SRCS test_main.c test_ui_dir.c
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../../../components/espos_httpd/src"
    PRIV_REQUIRES unity espos_httpd espressif__cjson
    WHOLE_ARCHIVE)
