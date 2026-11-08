#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "avrcp_common_utils.h"
#include "avrcp_cover_art_service.h"

#include "ui/lvgl.h"
#include "ui/screens/loader.h"
#include "ui/screens/main.h"
#include "ui/animations.h"

#define LCD_HOST       SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (60 * 1000 * 1000)
#define LCD_BK_LIGHT_ON_LEVEL  1
#define PIN_NUM_DATA0          23
#define PIN_NUM_PCLK           18
#define PIN_NUM_CS             15
#define PIN_NUM_DC             2
#define PIN_NUM_RST            4
#define PIN_NUM_BK_LIGHT       22
#define PIN_NUM_MISO           19
#define PIN_NUM_TOUCH_CS       32
#define PIN_NUM_TOUCH_IRQ      33

#define LCD_H_RES              320
#define LCD_V_RES              240

#define LCD_PANEL_NATIVE_H_RES 240
#define LCD_PANEL_NATIVE_V_RES 320

#define LCD_CMD_BITS           8
#define LCD_PARAM_BITS         8

#define LVGL_BUF_LINES 40

#define LCD_BK_LIGHT_LEDC_TIMER    LEDC_TIMER_0
#define LCD_BK_LIGHT_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define LCD_BK_LIGHT_LEDC_CHANNEL  LEDC_CHANNEL_0
#define LCD_BK_LIGHT_LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define LCD_BK_LIGHT_LEDC_FREQ_HZ  5000

/* persistent UI control block */
avrc_cover_art_ui_cb_t s_ui_cb;

/* Set backlight brightness, 0 (off) - 100 (full). */
void lvgl_set_backlight_brightness(uint8_t percent)
{
	if (percent > 100) percent = 100;
	if (percent == 0) percent = 1;

	uint32_t max_duty = (1u << LCD_BK_LIGHT_LEDC_DUTY_RES) - 1;
	uint32_t on_duty = (max_duty * percent) / 100;
	/* if the backlight is active-low, a higher "on-ness" means a lower duty */
	uint32_t duty = LCD_BK_LIGHT_ON_LEVEL ? on_duty : (max_duty - on_duty);

	ESP_ERROR_CHECK(ledc_set_duty(LCD_BK_LIGHT_LEDC_MODE, LCD_BK_LIGHT_LEDC_CHANNEL, duty));
	ESP_ERROR_CHECK(ledc_update_duty(LCD_BK_LIGHT_LEDC_MODE, LCD_BK_LIGHT_LEDC_CHANNEL));
}

void lvgl_ui_init(void)
{
	const ledc_timer_config_t bk_ledc_timer = {
			.speed_mode = LCD_BK_LIGHT_LEDC_MODE,
			.duty_resolution = LCD_BK_LIGHT_LEDC_DUTY_RES,
			.timer_num = LCD_BK_LIGHT_LEDC_TIMER,
			.freq_hz = LCD_BK_LIGHT_LEDC_FREQ_HZ,
			.clk_cfg = LEDC_AUTO_CLK,
	};
	ESP_ERROR_CHECK(ledc_timer_config(&bk_ledc_timer));

	const ledc_channel_config_t bk_ledc_channel = {
			.gpio_num = PIN_NUM_BK_LIGHT,
			.speed_mode = LCD_BK_LIGHT_LEDC_MODE,
			.channel = LCD_BK_LIGHT_LEDC_CHANNEL,
			.intr_type = LEDC_INTR_DISABLE,
			.timer_sel = LCD_BK_LIGHT_LEDC_TIMER,
			/* Turn off backlight to avoid unpredictable display on the LCD screen while
			 * initializing the LCD panel driver. (Different LCD screens may need different levels) */
			.duty = LCD_BK_LIGHT_ON_LEVEL ? 0 : (1u << LCD_BK_LIGHT_LEDC_DUTY_RES) - 1,
			.hpoint = 0,
	};
	/* Initialize the LEDC PWM channel driving the backlight */
	ESP_ERROR_CHECK(ledc_channel_config(&bk_ledc_channel));

	spi_bus_config_t buscfg = {
			.sclk_io_num = PIN_NUM_PCLK,
			.mosi_io_num = PIN_NUM_DATA0,
			.miso_io_num = PIN_NUM_MISO,
			.quadwp_io_num = -1,
			.quadhd_io_num = -1,
			.max_transfer_sz = LVGL_BUF_LINES * LCD_H_RES * sizeof(uint16_t) + 8};
	/* Initialize the SPI bus (shared by the LCD and the XPT2046 touch controller) */
	ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

	esp_lcd_panel_io_spi_config_t io_config = {
			.dc_gpio_num = PIN_NUM_DC,
			.cs_gpio_num = PIN_NUM_CS,
			.pclk_hz = LCD_PIXEL_CLOCK_HZ,
			.lcd_cmd_bits = LCD_CMD_BITS,
			.lcd_param_bits = LCD_PARAM_BITS,
			.spi_mode = 0,
			.trans_queue_depth = 10,
	};
	/* Attach the LCD to the SPI bus */
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_ui_cb.io_handle));

	esp_lcd_panel_dev_config_t panel_config = {
			.reset_gpio_num = PIN_NUM_RST,
			.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR, /* this ILI9341 module's subpixel order is BGR, not RGB */
			.bits_per_pixel = 16,
	};
	/* Initialize the LCD configuration */
	ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(s_ui_cb.io_handle, &panel_config, &s_ui_cb.panel_handle));

	/* Reset the display */
	ESP_ERROR_CHECK(esp_lcd_panel_reset(s_ui_cb.panel_handle));

	/* Initialize LCD panel */
	ESP_ERROR_CHECK(esp_lcd_panel_init(s_ui_cb.panel_handle));

	ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_ui_cb.panel_handle, false));

	ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_ui_cb.panel_handle, true, false));

	/* Turn on the screen */
	ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_ui_cb.panel_handle, true));

	esp_lcd_panel_io_spi_config_t tp_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_NUM_TOUCH_CS);
	esp_lcd_panel_io_handle_t tp_io_handle = NULL;
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &tp_io_config, &tp_io_handle));

	esp_lcd_touch_config_t tp_cfg = {
			.x_max = LCD_PANEL_NATIVE_H_RES,
			.y_max = LCD_PANEL_NATIVE_V_RES,
			.rst_gpio_num = -1,
			.int_gpio_num = PIN_NUM_TOUCH_IRQ,
			.flags = {
					.swap_xy = 0,
					.mirror_x = 0,
					.mirror_y = 1,
			},
	};
	ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &s_ui_cb.tp_handle));

	/* Bring up esp_lvgl_port: dedicated LVGL task pinned to core 1 (the BT stack
	 * is pinned to core 0 via CONFIG_BT_BLUEDROID_PINNED_TO_CORE_0) */
	const lvgl_port_cfg_t lvgl_cfg = {
			.task_priority = 4,
			.task_stack = 6144,
			.task_affinity = 1,
			.task_max_sleep_ms = 500,
			.timer_period_ms = 5,
	};
	ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

	const lvgl_port_display_cfg_t disp_cfg = {
			.io_handle = s_ui_cb.io_handle,
			.panel_handle = s_ui_cb.panel_handle,
			.buffer_size = LCD_PANEL_NATIVE_H_RES * LVGL_BUF_LINES,
			.double_buffer = true,
			.hres = LCD_PANEL_NATIVE_H_RES, /* native panel orientation: 240x320 portrait */
			.vres = LCD_PANEL_NATIVE_V_RES,
			.color_format = LV_COLOR_FORMAT_RGB565,
			.flags = {
					.buff_dma = true,
					.buff_spiram = false, /* plain ESP32 (unlike S2/S3) cannot DMA directly out of PSRAM */
					.sw_rotate = true,		/* rotate to 320x240 landscape in software, per-chunk */
					.swap_bytes = true,		/* SPI panels expect big-endian RGB565 on the wire */
			},
	};
	s_ui_cb.disp_handle = lvgl_port_add_disp(&disp_cfg);

	s_ui_cb.decode_sem = xSemaphoreCreateBinary();
	s_ui_cb.pending_mutex = xSemaphoreCreateMutex();

	lv_display_set_rotation(s_ui_cb.disp_handle, LV_DISPLAY_ROTATION_90);

	const lvgl_port_touch_cfg_t touch_cfg = {
			.disp = s_ui_cb.disp_handle,
			.handle = s_ui_cb.tp_handle,
	};
	s_ui_cb.touch_indev = lvgl_port_add_touch(&touch_cfg);

	if (lvgl_port_lock(0))
	{
		anim_text_scroll_init();
		ui_main_screen_init();
		avrc_cover_art_decode_create_task();
		
		lvgl_port_unlock();
	}

	lvgl_set_backlight_brightness(50);
}