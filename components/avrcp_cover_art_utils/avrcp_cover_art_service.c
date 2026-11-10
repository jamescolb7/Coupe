/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lvgl_port.h"
#include "jpeg_decoder.h"
#include "avrcp_common_utils.h"
#include "avrcp_cover_art_service.h"
#include "ui/lvgl.h"
#include "state/player.h"

/* tags*/
#define RC_CA_SRV_TAG  "RC_CA_SRV"

// Using SPI2 (VSPI) for both the LCD and the touch controller


#define COVER_ART_MAX_DECODE_W        200
#define COVER_ART_MAX_DECODE_H        200

/**
 * @brief  Persistent, boot-lifetime UI state: display, touch, LVGL objects and
 *         the handoff primitives between the BT stack and the decode task.
 *         Set up once by avrc_cover_art_srv_ui_init() and never torn down.
 */

/**
 * @brief  AVRCP cover art per-connection control block. Reset on every
 *         connect/disconnect; carries no display/UI state.
 */
typedef struct {
    bool connected;                          /* Connection status flag */
    bool getting;                            /* Flag indicating if image is being retrieved */
    /* Related to the image */
    uint8_t image_hdl_old[7];                /* Previous image handle, used to detect image changes */
    uint32_t image_size;                     /* Size of the image data in bytes */
    uint8_t *image_data;                     /* Pointer to the image data buffer */
    bool image_final;                        /* Indicate whether the image reception has been completed */
} avrc_cover_art_srv_cb_t;

/*******************************
 * STATIC FUNCTION DECLARATIONS
 ******************************/

/* image handle check */
static bool avrc_cover_art_srv_image_handle_check(uint8_t *image_handle, int len);
/* free image data */
static void avrc_cover_art_srv_free_image_data(void);
/* decode task: consumes raw JPEG buffers handed off from the AVRCP event path */
static void cover_art_decode_task(void *arg);

/*******************************
 * STATIC VARIABLE DEFINITIONS
 ******************************/


/* avrcp cover art per-connection control block */
static avrc_cover_art_srv_cb_t s_avrc_cover_art_srv_cb;

/********************************
 * STATIC FUNCTION DEFINITIONS
 *******************************/

static bool avrc_cover_art_srv_image_handle_check(uint8_t *image_handle, int len)
{
    /* Image handle length must be 7 */
    if (len == 7 && memcmp(s_avrc_cover_art_srv_cb.image_hdl_old, image_handle, 7) != 0) {
        memcpy(s_avrc_cover_art_srv_cb.image_hdl_old, image_handle, 7);
        return true;
    }
    return false;
}

static void avrc_cover_art_srv_free_image_data(void)
{
    if (s_avrc_cover_art_srv_cb.image_data) {
        free(s_avrc_cover_art_srv_cb.image_data);
        s_avrc_cover_art_srv_cb.image_data = NULL;
    }
    s_avrc_cover_art_srv_cb.image_size = 0;
}

static void cover_art_decode_task(void *arg)
{
    for (;;) {
        xSemaphoreTake(s_ui_cb.decode_sem, portMAX_DELAY);

        xSemaphoreTake(s_ui_cb.pending_mutex, portMAX_DELAY);
        uint8_t *jpeg_data = s_ui_cb.pending_data;
        uint32_t jpeg_size = s_ui_cb.pending_size;
        s_ui_cb.pending_data = NULL;
        s_ui_cb.pending_size = 0;
        xSemaphoreGive(s_ui_cb.pending_mutex);

        if (!jpeg_data) {
            continue;
        }

        uint16_t *pixels = calloc(COVER_ART_MAX_DECODE_W * COVER_ART_MAX_DECODE_H, sizeof(uint16_t));
        if (!pixels) {
            ESP_LOGE(RC_CA_SRV_TAG, "no mem for cover art decode buffer");
            free(jpeg_data);
            continue;
        }

        /* Decode to plain (non-byte-swapped) RGB565: this buffer feeds LVGL's
         * lv_image widget, not the panel directly, so it must stay in LVGL's
         * native pixel format. esp_lvgl_port's disp flags.swap_bytes handles
         * any byte-order conversion the panel needs at flush time instead. */
        esp_jpeg_image_cfg_t jpeg_cfg = {
            .indata = jpeg_data,
            .indata_size = jpeg_size,
            .outbuf = (uint8_t *)pixels,
            .outbuf_size = COVER_ART_MAX_DECODE_W * COVER_ART_MAX_DECODE_H * sizeof(uint16_t),
            .out_format = JPEG_IMAGE_FORMAT_RGB565,
            .out_scale = JPEG_IMAGE_SCALE_0,
            .flags = {
                .swap_color_bytes = 0,
            }
        };
        esp_jpeg_image_output_t outimg;
        esp_err_t ret = esp_jpeg_decode(&jpeg_cfg, &outimg);
        if (ret != ESP_OK) {
            /* source image likely larger than our decode cap at native size; retry downscaled */
            jpeg_cfg.out_scale = JPEG_IMAGE_SCALE_1_2;
            ret = esp_jpeg_decode(&jpeg_cfg, &outimg);
        }
        free(jpeg_data);

        if (ret != ESP_OK) {
            ESP_LOGE(RC_CA_SRV_TAG, "JPEG decode failed: %d", ret);
            free(pixels);
            continue;
        }
        ESP_LOGI(RC_CA_SRV_TAG, "Cover art decoded: %dpx x %dpx", outimg.width, outimg.height);

        player_set_cover(pixels, outimg.width, outimg.height);
    }
}

/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/

void avrc_cover_art_srv_open(void)
{
    memset(&s_avrc_cover_art_srv_cb, 0, sizeof(avrc_cover_art_srv_cb_t));
    /* display/touch/LVGL are already up (brought up once at boot by avrc_cover_art_srv_ui_init) */
}

void avrc_cover_art_srv_close(void)
{
    avrc_cover_art_srv_free_image_data();
    memset(&s_avrc_cover_art_srv_cb, 0, sizeof(avrc_cover_art_srv_cb_t));
    /* intentionally does not touch the display/touch/LVGL state in s_ui_cb */
}

void avrc_cover_art_srv_connect(uint16_t mtu)
{
    if (!s_avrc_cover_art_srv_cb.connected) {
        ESP_LOGW(RC_CA_SRV_TAG, "Start cover art connection...");
        /* start the cover art connection */
        esp_avrc_ct_cover_art_connect(mtu);
    }
}

void avrc_cover_art_srv_set_image_final(bool final)
{
    s_avrc_cover_art_srv_cb.image_final = final;
    if (!s_avrc_cover_art_srv_cb.image_final) {
        return;
    }
    ESP_LOGI(RC_CA_SRV_TAG, "Cover Art Client final data event, image size: %lu bytes", s_avrc_cover_art_srv_cb.image_size);

    /* hand off ownership of the raw JPEG buffer to the decode task; drop any
     * still-pending (undecoded) buffer from a previous, now-superseded image */
    xSemaphoreTake(s_ui_cb.pending_mutex, portMAX_DELAY);
    if (s_ui_cb.pending_data) {
        free(s_ui_cb.pending_data);
    }
    s_ui_cb.pending_data = s_avrc_cover_art_srv_cb.image_data;
    s_ui_cb.pending_size = s_avrc_cover_art_srv_cb.image_size;
    xSemaphoreGive(s_ui_cb.pending_mutex);

    /* the per-connection control block no longer owns this buffer */
    s_avrc_cover_art_srv_cb.image_data = NULL;
    s_avrc_cover_art_srv_cb.image_size = 0;
    /* set the getting state to false, we can get next image now */
    s_avrc_cover_art_srv_cb.getting = false;

    xSemaphoreGive(s_ui_cb.decode_sem);
}

void avrc_cover_art_srv_set_connected(bool connected)
{
    s_avrc_cover_art_srv_cb.connected = connected;
}

void avrc_cover_art_srv_ca_req(void)
{
    /* request cover art */
    if (s_avrc_cover_art_srv_cb.connected) {
        uint8_t attr_mask = ESP_AVRC_MD_ATTR_COVER_ART;

        esp_avrc_ct_send_metadata_cmd(bt_avrc_common_alloc_tl(), attr_mask);
    }
}

void avrc_cover_art_srv_save_image_data(uint8_t *p_data, uint16_t data_len)
{
    if (s_avrc_cover_art_srv_cb.image_size > UINT32_MAX - data_len) {
        ESP_LOGE(RC_CA_SRV_TAG, "Image size overflow");
        avrc_cover_art_srv_free_image_data();
        return;
    }
    s_avrc_cover_art_srv_cb.image_size += data_len;

    uint8_t *p_buf = (uint8_t *)realloc(s_avrc_cover_art_srv_cb.image_data, s_avrc_cover_art_srv_cb.image_size * sizeof(uint8_t));
    if (!p_buf) {
        ESP_LOGE(RC_CA_SRV_TAG, "%s: The memory allocation of Cover art image data failed", __func__);
        avrc_cover_art_srv_free_image_data();
        return;
    }
    s_avrc_cover_art_srv_cb.image_data = p_buf;
    memcpy(s_avrc_cover_art_srv_cb.image_data + s_avrc_cover_art_srv_cb.image_size - data_len, p_data, data_len);
}

void avrc_cover_art_srv_ct_metadata_update(uint8_t *image_handle, int len)
{
    if (s_avrc_cover_art_srv_cb.connected && !s_avrc_cover_art_srv_cb.getting) {
        /* check image handle is valid and different with last one, we don't want to get an image repeatedly */
        if (avrc_cover_art_srv_image_handle_check(image_handle, len)) {
            /* free the previous image data */
            avrc_cover_art_srv_free_image_data();
            /* get the linked thumbnail */
            esp_avrc_ct_cover_art_get_linked_thumbnail(image_handle);
            s_avrc_cover_art_srv_cb.getting = true;
        }
    }
}

void avrc_cover_art_decode_create_task(void) {
    xTaskCreatePinnedToCore(cover_art_decode_task, "CoverArtDecode", 6144, NULL, 4, &s_ui_cb.decode_task_handle, 1);
}