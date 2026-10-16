/*
 * SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * The linux target has no card; the sessions run on any directory instead.
 */
#include <stddef.h>

#include "espos_sdlog_card.h"

esp_err_t espos_sdlog_card_mount(const espos_sdlog_card_cfg_t *cfg)
{
    (void)cfg;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t espos_sdlog_card_unmount(void)
{
    return ESP_ERR_INVALID_STATE;
}

bool espos_sdlog_card_mounted(void)
{
    return false;
}

const char *espos_sdlog_card_mount_point(void)
{
    return NULL;
}

esp_err_t espos_sdlog_card_space(uint64_t *total, uint64_t *free_bytes)
{
    (void)total;
    (void)free_bytes;
    return ESP_ERR_INVALID_STATE;
}
