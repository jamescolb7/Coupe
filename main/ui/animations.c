#include "esp_err.h"
#include "esp_lvgl_port.h"

#include "animations.h"
#include "screens/main.h"

static lv_anim_t anim_text_scroll;
lv_style_t anim_text_scroll_sync;

void anim_text_scroll_init() {
	lv_anim_init(&anim_text_scroll);
	lv_anim_set_delay(&anim_text_scroll, 2000);
	lv_anim_set_repeat_delay(&anim_text_scroll, 6000);

	lv_anim_set_repeat_count(&anim_text_scroll, LV_ANIM_REPEAT_INFINITE);

	lv_style_init(&anim_text_scroll_sync);
	lv_style_set_anim(&anim_text_scroll_sync, &anim_text_scroll);

	lv_style_set_anim_duration(&anim_text_scroll_sync, 6000);
}

void ui_marquee_resync(void) {
	lv_label_set_long_mode(ui_title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_label_set_long_mode(ui_subtitle_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
}