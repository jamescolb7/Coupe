/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio/bt_app_av.h"
#include "a2dp_utils_tags.h"
#include "avrcp_cover_art_service.h"

#include "audio/bt_app_av.h"
#include "ui/lvgl.h"

void app_main(void)
{
    /* bring up display/touch/LVGL immediately, independent of BT connection state */
    lvgl_ui_init();

    bt_app_init();
    

    // create_task();
}
