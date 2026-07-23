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
// #include "esp_lcd_panel_io.h"
// #include "esp_lcd_panel_ops.h"
// #include "esp_lcd_ili9341.h"
// #include "esp_lcd_touch.h"
// #include "esp_lcd_touch_xpt2046.h"
// #include "esp_lvgl_port.h"
// #include "lvgl.h"
#include "esp_lvgl_port.h"
#include "jpeg_decoder.h"
#include "avrcp_common_utils.h"
#include "avrcp_cover_art_service.h"
#include "ui/lvgl.h"

#include "ui/screens/main.h"

/* tags*/
#define RC_CA_SRV_TAG  "RC_CA_SRV"

// Using SPI2 (VSPI) for both the LCD and the touch controller


#define COVER_ART_MAX_DECODE_W        300
#define COVER_ART_MAX_DECODE_H        300

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

        if (lvgl_port_lock(0)) {
            if (s_ui_cb.img_dsc.data) {
                free((void *)s_ui_cb.img_dsc.data);
            }
            s_ui_cb.img_dsc.header.w = outimg.width;
            s_ui_cb.img_dsc.header.h = outimg.height;
            s_ui_cb.img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
            s_ui_cb.img_dsc.header.stride = outimg.width * sizeof(uint16_t);
            s_ui_cb.img_dsc.data_size = (uint32_t)outimg.width * outimg.height * sizeof(uint16_t);
            s_ui_cb.img_dsc.data = (uint8_t *)pixels;

            lv_image_set_src(ui_album_image, &s_ui_cb.img_dsc);



            // /* scale to fit the 320x240 screen, preserving aspect ratio, never upscaling */
            // uint32_t scale_w = (LCD_H_RES * 256) / outimg.width;
            // uint32_t scale_h = (LCD_H_RES * 256) / outimg.height;
            // uint32_t scale = (scale_w < scale_h) ? scale_w : scale_h;
            // if (scale > 256) {
            //     scale = 256;
            // }
            // lv_image_set_scale(s_ui_cb.img_obj, scale);
            // lv_obj_center(s_ui_cb.img_obj);

            // lv_obj_add_flag(s_ui_cb.placeholder_label, LV_OBJ_FLAG_HIDDEN);
            // lv_obj_clear_flag(s_ui_cb.img_obj, LV_OBJ_FLAG_HIDDEN);

            lvgl_port_unlock();
        } else {
            free(pixels);
        }
    }
}

/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/

// void avrc_cover_art_srv_ui_init(void)
// {
//     gpio_config_t bk_gpio_config = {
//         .mode = GPIO_MODE_OUTPUT,
//         .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
//     };
//     /* Initialize the GPIO of backlight */
//     ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

//     /* Turn off backlight to avoid unpredictable display on the LCD screen while initializing
//      * the LCD panel driver. (Different LCD screens may need different levels) */
//     ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL));

//     spi_bus_config_t buscfg = {
//         .sclk_io_num = EXAMPLE_PIN_NUM_PCLK,
//         .mosi_io_num = EXAMPLE_PIN_NUM_DATA0,
//         .miso_io_num = EXAMPLE_PIN_NUM_MISO,
//         .quadwp_io_num = -1,
//         .quadhd_io_num = -1,
//         .max_transfer_sz = CONFIG_EXAMPLE_LVGL_BUF_LINES * EXAMPLE_LCD_H_RES * sizeof(uint16_t) + 8
//     };
//     /* Initialize the SPI bus (shared by the LCD and the XPT2046 touch controller) */
//     ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

//     esp_lcd_panel_io_spi_config_t io_config = {
//         .dc_gpio_num = EXAMPLE_PIN_NUM_DC,
//         .cs_gpio_num = EXAMPLE_PIN_NUM_CS,
//         .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
//         .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
//         .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
//         .spi_mode = 0,
//         .trans_queue_depth = 10,
//     };
//     /* Attach the LCD to the SPI bus */
//     ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_ui_cb.io_handle));

//     esp_lcd_panel_dev_config_t panel_config = {
//         .reset_gpio_num = EXAMPLE_PIN_NUM_RST,
//         .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,  /* this ILI9341 module's subpixel order is BGR, not RGB */
//         .bits_per_pixel = 16,
//     };
//     /* Initialize the LCD configuration */
//     ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(s_ui_cb.io_handle, &panel_config, &s_ui_cb.panel_handle));

//     /* Reset the display */
//     ESP_ERROR_CHECK(esp_lcd_panel_reset(s_ui_cb.panel_handle));

//     /* Initialize LCD panel */
//     ESP_ERROR_CHECK(esp_lcd_panel_init(s_ui_cb.panel_handle));

//     /* Most ILI9341 modules do not need color inversion (unlike the ST7789 this
//      * example originally targeted) -- verify visually and flip if colors look wrong. */
//     ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_ui_cb.panel_handle, false));

//     /* Unlike swap_xy, mirroring just reverses the address-increment direction
//      * on one axis -- it doesn't need cross-axis data reordering, so it's safe
//      * to combine with a partial/chunked flush buffer. This corrects a plain
//      * left-right (mirror-image) flip left over after the software rotation
//      * above; if it's still mirrored (or now mirrored the other way), swap
//      * these two booleans. */
//     ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_ui_cb.panel_handle, true, false));

//     /* Deliberately NOT calling esp_lcd_panel_swap_xy() here: combined with a
//      * partial/chunked LVGL flush buffer (required -- a full 320x240 buffer
//      * doesn't fit in internal DRAM), swap_xy only repositions the panel's
//      * addressing window, not the pixel data layout per chunk, which corrupts
//      * the image in content-dependent bands. Instead the panel stays in its
//      * native 240x320 portrait orientation, and LVGL performs the 320x240
//      * landscape rotation itself in software (flags.sw_rotate below), which
//      * correctly rotates each partial chunk before it's sent to the panel. */

//     /* Turn on the screen */
//     ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_ui_cb.panel_handle, true));

//     /* Bring up the XPT2046 touch controller, sharing the LCD's SPI bus via its own CS line */
//     esp_lcd_panel_io_spi_config_t tp_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(EXAMPLE_PIN_NUM_TOUCH_CS);
//     esp_lcd_panel_io_handle_t tp_io_handle = NULL;
//     ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &tp_io_config, &tp_io_handle));

//     esp_lcd_touch_config_t tp_cfg = {
//         .x_max = EXAMPLE_LCD_PANEL_NATIVE_H_RES,
//         .y_max = EXAMPLE_LCD_PANEL_NATIVE_V_RES,
//         .rst_gpio_num = -1,   /* XPT2046 has no reset line */
//         .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_IRQ,
//         .flags = {
//             /* raw touch coords are in the panel's native (un-rotated) frame;
//              * esp_lvgl_port remaps them using the display's active rotation
//              * (set via lv_display_set_rotation below), so no swap/mirror here
//              * unless the touch panel itself is physically mounted flipped
//              * relative to the LCD glass -- verify empirically. */
//             .swap_xy = 0,
//             .mirror_x = 0,
//             .mirror_y = 0,
//         },
//     };
//     ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &s_ui_cb.tp_handle));

//     /* Bring up esp_lvgl_port: dedicated LVGL task pinned to core 1 (the BT stack
//      * is pinned to core 0 via CONFIG_BT_BLUEDROID_PINNED_TO_CORE_0) */
//     const lvgl_port_cfg_t lvgl_cfg = {
//         .task_priority = 4,
//         .task_stack = 6144,
//         .task_affinity = 1,
//         .task_max_sleep_ms = 500,
//         .timer_period_ms = 5,
//     };
//     ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

//     const lvgl_port_display_cfg_t disp_cfg = {
//         .io_handle = s_ui_cb.io_handle,
//         .panel_handle = s_ui_cb.panel_handle,
//         .buffer_size = EXAMPLE_LCD_PANEL_NATIVE_H_RES * CONFIG_EXAMPLE_LVGL_BUF_LINES,
//         .double_buffer = true,
//         .hres = EXAMPLE_LCD_PANEL_NATIVE_H_RES,  /* native panel orientation: 240x320 portrait */
//         .vres = EXAMPLE_LCD_PANEL_NATIVE_V_RES,
//         .color_format = LV_COLOR_FORMAT_RGB565,
//         .flags = {
//             .buff_dma = true,
//             .buff_spiram = false, /* plain ESP32 (unlike S2/S3) cannot DMA directly out of PSRAM */
//             .sw_rotate = true,    /* rotate to 320x240 landscape in software, per-chunk */
//             .swap_bytes = true,   /* SPI panels expect big-endian RGB565 on the wire */
//         },
//     };
//     s_ui_cb.disp_handle = lvgl_port_add_disp(&disp_cfg);

//     /* Logically rotate to 320x240 landscape for the rest of the UI code.
//      * Direction (90 vs 270) is hardware-mount dependent -- verify visually
//      * and flip to LV_DISPLAY_ROTATION_270 if the image appears upside down. */
//     lv_display_set_rotation(s_ui_cb.disp_handle, LV_DISPLAY_ROTATION_90);

//     const lvgl_port_touch_cfg_t touch_cfg = {
//         .disp = s_ui_cb.disp_handle,
//         .handle = s_ui_cb.tp_handle,
//     };
//     s_ui_cb.touch_indev = lvgl_port_add_touch(&touch_cfg);

//     if (lvgl_port_lock(0)) {
//         /* explicit, opaque background: the cover art is scale-to-fit and
//          * centered, so any letterbox/pillarbox margin around a non-4:3 image
//          * shows this color instead of whatever the panel's power-on RAM held */
//         lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
//         lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

//         s_ui_cb.placeholder_label = lv_label_create(lv_scr_act());
//         lv_label_set_text(s_ui_cb.placeholder_label, "Waiting for cover art...");
//         lv_obj_center(s_ui_cb.placeholder_label);

//         s_ui_cb.img_obj = lv_image_create(lv_scr_act());
//         lv_obj_add_flag(s_ui_cb.img_obj, LV_OBJ_FLAG_HIDDEN);

//         lvgl_port_unlock();
//     }

//     /* Turn on backlight now that valid content is on screen
//      * (Different LCD screens may need different levels) */
//     ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL));

//     /* handoff primitives + dedicated decode task, keeps JPEG decode and LVGL
//      * calls off the shared, small-stack BtAppTask */
//     s_ui_cb.decode_sem = xSemaphoreCreateBinary();
//     s_ui_cb.pending_mutex = xSemaphoreCreateMutex();
//     xTaskCreatePinnedToCore(cover_art_decode_task, "CoverArtDecode", 6144, NULL, 4, &s_ui_cb.decode_task_handle, 1);
// }

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

void create_task(void) {
    xTaskCreatePinnedToCore(cover_art_decode_task, "CoverArtDecode", 6144, NULL, 4, &s_ui_cb.decode_task_handle, 1);
}