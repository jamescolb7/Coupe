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
	/* lv_anim_init() defaults repeat_cnt to 1 (play once). The label widget
	 * copies repeat_cnt from this template onto its own scroll animation
	 * (overwrite_anim_property() in lv_label.c), clobbering LVGL's own
	 * LV_ANIM_REPEAT_INFINITE default -- without this the marquee scrolls
	 * once and stops. */
	lv_anim_set_repeat_count(&anim_text_scroll, LV_ANIM_REPEAT_INFINITE);

	lv_style_init(&anim_text_scroll_sync);
	lv_style_set_anim(&anim_text_scroll_sync, &anim_text_scroll);

	/* LVGL otherwise derives each label's scroll duration from that label's
	 * own overflow pixels, so title/subtitle would run different-length
	 * cycles even with this same style applied to both -- force a fixed
	 * duration so their cycles stay the same length. */
	lv_style_set_anim_duration(&anim_text_scroll_sync, 6000);
}

void ui_marquee_resync(void) {
	lv_label_set_long_mode(ui_title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_label_set_long_mode(ui_subtitle_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
}