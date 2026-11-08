#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio/bluetooth/sink.h"
#include "a2dp_utils_tags.h"
#include "avrcp_cover_art_service.h"

#include "ui/lvgl.h"

void app_main(void)
{
    lvgl_ui_init();
    bt_app_init();
}
