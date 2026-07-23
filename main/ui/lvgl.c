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

#define LCD_HOST       SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)
#define LCD_BK_LIGHT_ON_LEVEL  0
#define LCD_BK_LIGHT_OFF_LEVEL !LCD_BK_LIGHT_ON_LEVEL
#define PIN_NUM_DATA0          23  /*!< for 1-line SPI, this also referred as MOSI */
#define PIN_NUM_PCLK           18
#define PIN_NUM_CS             15
#define PIN_NUM_DC             2
#define PIN_NUM_RST            4
#define PIN_NUM_BK_LIGHT       5
#define PIN_NUM_MISO           19   /*!< shared SPI bus MISO, needed for touch */
#define PIN_NUM_TOUCH_CS       32
#define PIN_NUM_TOUCH_IRQ      33

// Logical UI resolution (landscape, after LVGL's software rotation)
#define LCD_H_RES              320
#define LCD_V_RES              240
// The ILI9341's native physical orientation (portrait) -- LVGL rotates to the
// logical landscape resolution above in software; see avrc_cover_art_srv_ui_init()
#define LCD_PANEL_NATIVE_H_RES 240
#define LCD_PANEL_NATIVE_V_RES 320
// Bit number used to represent command and parameter
#define LCD_CMD_BITS           8
#define LCD_PARAM_BITS         8

#define LVGL_BUF_LINES 40

/* Upper bound on the decoded cover-art thumbnail dimensions. AVRCP BIP "linked
 * thumbnail" images are conventionally small (commonly <=200x200); this cap
 * just bounds the worst-case decode buffer size and is independent of the
 * panel resolution. LVGL scales the decoded image to fit the 320x240 screen
 * regardless of its native aspect ratio. */

/* persistent UI control block */
avrc_cover_art_ui_cb_t s_ui_cb;

void lvgl_ui_init(void)
{
	gpio_config_t bk_gpio_config = {
			.mode = GPIO_MODE_OUTPUT,
			.pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT};
	/* Initialize the GPIO of backlight */
	ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

	/* Turn off backlight to avoid unpredictable display on the LCD screen while initializing
	 * the LCD panel driver. (Different LCD screens may need different levels) */
	ESP_ERROR_CHECK(gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_OFF_LEVEL));

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

	/* Most ILI9341 modules do not need color inversion (unlike the ST7789 this
	 * example originally targeted) -- verify visually and flip if colors look wrong. */
	ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_ui_cb.panel_handle, false));

	/* Unlike swap_xy, mirroring just reverses the address-increment direction
	 * on one axis -- it doesn't need cross-axis data reordering, so it's safe
	 * to combine with a partial/chunked flush buffer. This corrects a plain
	 * left-right (mirror-image) flip left over after the software rotation
	 * above; if it's still mirrored (or now mirrored the other way), swap
	 * these two booleans. */
	ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_ui_cb.panel_handle, true, false));

	/* Deliberately NOT calling esp_lcd_panel_swap_xy() here: combined with a
	 * partial/chunked LVGL flush buffer (required -- a full 320x240 buffer
	 * doesn't fit in internal DRAM), swap_xy only repositions the panel's
	 * addressing window, not the pixel data layout per chunk, which corrupts
	 * the image in content-dependent bands. Instead the panel stays in its
	 * native 240x320 portrait orientation, and LVGL performs the 320x240
	 * landscape rotation itself in software (flags.sw_rotate below), which
	 * correctly rotates each partial chunk before it's sent to the panel. */

	/* Turn on the screen */
	ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_ui_cb.panel_handle, true));

	/* Bring up the XPT2046 touch controller, sharing the LCD's SPI bus via its own CS line */
	esp_lcd_panel_io_spi_config_t tp_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_NUM_TOUCH_CS);
	esp_lcd_panel_io_handle_t tp_io_handle = NULL;
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &tp_io_config, &tp_io_handle));

	esp_lcd_touch_config_t tp_cfg = {
			.x_max = LCD_PANEL_NATIVE_H_RES,
			.y_max = LCD_PANEL_NATIVE_V_RES,
			.rst_gpio_num = -1, /* XPT2046 has no reset line */
			/* Wired to the PENIRQ line: with CONFIG_XPT2046_INTERRUPT_MODE
			 * enabled, esp_lvgl_port switches the touch indev to
			 * LV_INDEV_MODE_EVENT and only reads on a PENIRQ edge, instead of
			 * polling every LVGL tick (~5ms). The LCD (queued/DMA color
			 * transfers) and this touch controller (blocking polling reads)
			 * share one physical SPI bus; polling on every tick left a wide,
			 * constantly-open window for the touch read to race the LCD's
			 * still in-flight queued transactions, which under heavy BT/audio
			 * load could wedge the shared bus and freeze the whole display
			 * (esp_lvgl_port's render task holds the LVGL mutex the entire
			 * time it's blocked inside that read). Event mode shrinks that
			 * window to "only while a finger is actually on the glass". */
			.int_gpio_num = PIN_NUM_TOUCH_IRQ,
			.flags = {
					/* raw touch coords are in the panel's native (un-rotated) frame;
					 * LVGL's rotation remap (lv_indev.c, driven by
					 * lv_display_set_rotation below) then maps that native frame
					 * into the 320x240 logical landscape frame. That remap only
					 * accounts for the LCD's own rotation -- it assumes the touch
					 * digitizer's native X/Y axes already line up with the LCD
					 * panel's native address raster. On this hardware they don't:
					 * empirically, a native-frame Y mirror was required to match
					 * touch position to the button. With rotation=90, mirroring Y
					 * in the native frame ends up flipping left/right in the final
					 * logical frame while leaving vertical position unaffected,
					 * which is exactly the left/right mismatch this corrects for.
					 * If the mount changes, re-derive empirically. */
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

	/* handoff primitives must exist before create_task() below spins up
	 * cover_art_decode_task, which blocks on decode_sem immediately -- taking
	 * a NULL semaphore/mutex crashes FreeRTOS and manifests as a boot loop */
	s_ui_cb.decode_sem = xSemaphoreCreateBinary();
	s_ui_cb.pending_mutex = xSemaphoreCreateMutex();

	/* Logically rotate to 320x240 landscape for the rest of the UI code.
	 * Direction (90 vs 270) is hardware-mount dependent -- verify visually
	 * and flip to LV_DISPLAY_ROTATION_270 if the image appears upside down. */
	lv_display_set_rotation(s_ui_cb.disp_handle, LV_DISPLAY_ROTATION_90);

	const lvgl_port_touch_cfg_t touch_cfg = {
			.disp = s_ui_cb.disp_handle,
			.handle = s_ui_cb.tp_handle,
	};
	s_ui_cb.touch_indev = lvgl_port_add_touch(&touch_cfg);

	if (lvgl_port_lock(0))
	{
		/* explicit, opaque background: the cover art is scale-to-fit and
		 * centered, so any letterbox/pillarbox margin around a non-4:3 image
		 * shows this color instead of whatever the panel's power-on RAM held */
		// lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
		// lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

		// s_ui_cb.placeholder_label = lv_label_create(lv_scr_act());
		// lv_label_set_text(s_ui_cb.placeholder_label, "Waiting for cover art...");
		// lv_obj_center(s_ui_cb.placeholder_label);

		// s_ui_cb.img_obj = lv_image_create(lv_scr_act());
		// lv_obj_add_flag(s_ui_cb.img_obj, LV_OBJ_FLAG_HIDDEN);

		ui_Screen1_screen_init();
		create_task();
		
		lvgl_port_unlock();
	}

	/* Turn on backlight now that valid content is on screen
	 * (Different LCD screens may need different levels) */
	ESP_ERROR_CHECK(gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_ON_LEVEL));
}