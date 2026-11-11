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
#include "state/player.h"
#include "state/settings.h"
#include "audio/spectrum.h"
#include "nvs_flash.h"

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    nvs_init();
    settings_init();
    player_state_init();
    spectrum_init();
    lvgl_ui_init();
    bt_app_init();
}
