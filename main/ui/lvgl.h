#ifndef LVGL_UI_H
#define LVGL_UI_H

#include <stdint.h>
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include <lvgl.h>

#define LCD_H_RES 320
#define LCD_V_RES 240

typedef struct {
	esp_lcd_panel_io_handle_t io_handle;
	esp_lcd_panel_handle_t    panel_handle;
	esp_lcd_touch_handle_t    tp_handle;
	lv_display_t             *disp_handle;
	lv_indev_t               *touch_indev;
	lv_obj_t                 *img_obj;
	lv_obj_t                 *placeholder_label;
	lv_image_dsc_t            img_dsc;
	SemaphoreHandle_t         decode_sem;
	SemaphoreHandle_t         pending_mutex;
	uint8_t                  *pending_data;
	uint32_t                  pending_size;
	TaskHandle_t              decode_task_handle;
} avrc_cover_art_ui_cb_t;

extern avrc_cover_art_ui_cb_t s_ui_cb;

void lvgl_ui_init(void);

/* Set backlight brightness, 0 (off) - 100 (full). */
void lvgl_set_backlight_brightness(uint8_t percent);

#endif