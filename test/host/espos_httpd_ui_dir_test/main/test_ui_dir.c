/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * The bundle espos_httpd embeds is the directory ESPOS_HTTPD_UI_DIR names,
 * file for file: paths relative to it, ".gz" stripped and flagged, bytes as
 * on disk, and nothing from espOS's own ui-dist/. A plain file kept beside
 * its .gz (assets/tank-abc123.js) is left out, so one path has one row.
 */
#include <string.h>

#include "ui_bundle.h"
#include "unity.h"

static const espos_ui_file_t *find(const char *path)
{
    for (size_t i = 0; i < espos_ui_file_count; i++) {
        if (strcmp(espos_ui_files[i].path, path) == 0) {
            return &espos_ui_files[i];
        }
    }
    return NULL;
}

TEST_CASE("the named directory is the bundle, and only it", "[ui_dir]")
{
    TEST_ASSERT_EQUAL_size_t(3, espos_ui_file_count);
    TEST_ASSERT_NOT_NULL(find("/index.html"));
    TEST_ASSERT_NOT_NULL(find("/assets/tank-abc123.js"));
    TEST_ASSERT_NOT_NULL(find("/robots.txt"));
}

static const espos_ui_file_t *must_find(const char *path)
{
    const espos_ui_file_t *f = find(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    return f;
}

TEST_CASE("gzipped files are flagged, plain ones are not", "[ui_dir]")
{
    TEST_ASSERT_TRUE(must_find("/index.html")->gz);
    TEST_ASSERT_TRUE(must_find("/assets/tank-abc123.js")->gz);
    TEST_ASSERT_FALSE(must_find("/robots.txt")->gz);
}

TEST_CASE("the bytes are the file's", "[ui_dir]")
{
    static const char want[] = "plain fixture file, not gzipped\n";
    const espos_ui_file_t *f = must_find("/robots.txt");
    TEST_ASSERT_EQUAL_size_t(sizeof(want) - 1, (size_t)(f->end - f->start));
    TEST_ASSERT_EQUAL_MEMORY(want, f->start, sizeof(want) - 1);

    const espos_ui_file_t *gz = must_find("/index.html");
    TEST_ASSERT_TRUE(gz->end - gz->start > 2);
    TEST_ASSERT_EQUAL_HEX8(0x1f, gz->start[0]); /* gzip magic */
    TEST_ASSERT_EQUAL_HEX8(0x8b, gz->start[1]);
}
