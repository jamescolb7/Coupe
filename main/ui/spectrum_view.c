#include <stdint.h>
#include <string.h>

#include "esp_lvgl_port.h"

#include "ui/spectrum_view.h"
#include "ui/screens/main.h"
#include "audio/spectrum.h"
#include "state/settings.h"

/*
 * The bars are drawn directly by the panel's draw event rather than being
 * one LVGL object each: resizing an object repaints its whole old+new area,
 * which made every frame re-render (and re-send over SPI) most of the
 * panel. Here each update only invalidates the thin strip between a bar's
 * old and new top (and the old/new cap), so a frame is a few small strips.
 */

#define SPECTRUM_REFRESH_MS 15   /* ~66 fps; analysis produces ~86 frames/s */
#define BAR_GAP_PX          6
#define CAP_HEIGHT_PX       3
#define CAP_GAP_PX          2
#define BAR_MIN_PX          2    /* a sliver stays visible at silence */
#define TOP_MARGIN_PX       6

/* opaque colors only: no per-pixel blending */
#define BAR_COLOR           0xF0F0F0
#define CAP_COLOR           0x9A9A9A
#define BG_COLOR            0x101010

static lv_obj_t *s_panel;

/* geometry, relative to the panel's content area */
static int32_t s_area_h;      /* usable bar height */
static int32_t s_bottom;      /* y of the bars' baseline */
static int32_t s_bar_x[SPECTRUM_BANDS];
static int32_t s_bar_w;
static int32_t s_layout_w = -1;
static int32_t s_layout_h = -1;

/* what is currently drawn, in px */
static int32_t s_bar_h[SPECTRUM_BANDS];
static int32_t s_cap_h[SPECTRUM_BANDS]; /* cap bottom height above baseline */

static void layout_bars(void)
{
	lv_obj_update_layout(s_panel);
	int32_t w = lv_obj_get_content_width(s_panel);
	int32_t h = lv_obj_get_content_height(s_panel);
	if (w <= 0 || h <= 0 || (w == s_layout_w && h == s_layout_h)) {
		return;
	}
	s_layout_w = w;
	s_layout_h = h;
	s_bottom = h;
	s_area_h = h - TOP_MARGIN_PX - CAP_HEIGHT_PX - CAP_GAP_PX;

	int32_t slot = w / SPECTRUM_BANDS;
	int32_t x0 = (w - slot * SPECTRUM_BANDS) / 2; /* center the leftover */
	s_bar_w = slot - BAR_GAP_PX;
	for (int i = 0; i < SPECTRUM_BANDS; i++) {
		s_bar_x[i] = x0 + i * slot + BAR_GAP_PX / 2;
		s_bar_h[i] = BAR_MIN_PX;
		s_cap_h[i] = BAR_MIN_PX + CAP_GAP_PX;
	}
	lv_obj_invalidate(s_panel);
}

/* invalidate rows [y_from, y_to) above the baseline of bar i (heights) */
static void invalidate_rows(int i, int32_t h_lo, int32_t h_hi)
{
	if (h_hi <= h_lo) {
		return;
	}
	lv_area_t c;
	lv_obj_get_content_coords(s_panel, &c);
	lv_area_t a = {
		.x1 = c.x1 + s_bar_x[i],
		.x2 = c.x1 + s_bar_x[i] + s_bar_w - 1,
		.y1 = c.y1 + s_bottom - h_hi,
		.y2 = c.y1 + s_bottom - h_lo - 1,
	};
	lv_obj_invalidate_area(s_panel, &a);
}

static void draw_cb(lv_event_t *e)
{
	lv_layer_t *layer = lv_event_get_layer(e);
	lv_area_t c;
	lv_obj_get_content_coords(s_panel, &c);

	lv_draw_fill_dsc_t bar;
	lv_draw_fill_dsc_init(&bar);
	bar.color = lv_color_hex(BAR_COLOR);
	bar.opa = LV_OPA_COVER;

	lv_draw_fill_dsc_t cap;
	lv_draw_fill_dsc_init(&cap);
	cap.color = lv_color_hex(CAP_COLOR);
	cap.opa = LV_OPA_COVER;

	for (int i = 0; i < SPECTRUM_BANDS; i++) {
		int32_t x1 = c.x1 + s_bar_x[i];
		int32_t x2 = x1 + s_bar_w - 1;
		int32_t base = c.y1 + s_bottom; /* first row below the bar */

		lv_area_t b = { .x1 = x1, .x2 = x2, .y1 = base - s_bar_h[i], .y2 = base - 1 };
		lv_draw_fill(layer, &bar, &b);

		lv_area_t k = { .x1 = x1, .x2 = x2,
		                .y1 = base - s_cap_h[i] - CAP_HEIGHT_PX, .y2 = base - s_cap_h[i] - 1 };
		lv_draw_fill(layer, &cap, &k);
	}
}

static void update_active(void)
{
	bool on = settings_get_spectrum() && lv_tabview_get_tab_active(ui_TabView1) == 0;
	spectrum_set_active(on);
}

/* Children are positioned inside the parent's padding, so LV_PCT(100)
 * would leave the tab's padding uncovered -- and the zoomed cover art
 * (a sibling, clipped only to the tab's outer area) showed there as a
 * border on every side. Size the panel to the tab's full area and shift
 * it out over the padding instead. */
static void cover_whole_tab(void)
{
	lv_obj_update_layout(ui_Now_Playing);
	int32_t pad_l = lv_obj_get_style_pad_left(ui_Now_Playing, LV_PART_MAIN);
	int32_t pad_t = lv_obj_get_style_pad_top(ui_Now_Playing, LV_PART_MAIN);
	int32_t border = lv_obj_get_style_border_width(ui_Now_Playing, LV_PART_MAIN);
	lv_obj_set_pos(s_panel, -pad_l, -pad_t);
	lv_obj_set_size(s_panel, lv_obj_get_width(ui_Now_Playing) - 2 * border,
	                lv_obj_get_height(ui_Now_Playing) - 2 * border);
}

static void apply_setting(void)
{
	if (settings_get_spectrum()) {
		cover_whole_tab();
		lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_move_foreground(s_panel);
		layout_bars();
	} else {
		lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
	}
	update_active();
}

static void tab_changed_cb(lv_event_t *e)
{
	(void)e;
	update_active();
}

static void refresh_timer_cb(lv_timer_t *t)
{
	(void)t;
	if (!spectrum_is_active() || s_area_h <= 0) {
		return;
	}

	uint8_t levels[SPECTRUM_BANDS];
	uint8_t peaks[SPECTRUM_BANDS];
	spectrum_get(levels, peaks);

	for (int i = 0; i < SPECTRUM_BANDS; i++) {
		int32_t bh = BAR_MIN_PX + (int32_t)levels[i] * (s_area_h - BAR_MIN_PX) / 100;
		int32_t ph = BAR_MIN_PX + (int32_t)peaks[i] * (s_area_h - BAR_MIN_PX) / 100;
		if (ph < bh) {
			ph = bh;
		}
		int32_t ch = ph + CAP_GAP_PX;

		/* one area per bar per frame: the rows between the old and new bar
		 * top, plus the old and new cap rows. Merging keeps the number of
		 * separate redraw/flush operations per frame <= SPECTRUM_BANDS. */
		int32_t lo = INT32_MAX;
		int32_t hi = INT32_MIN;
		if (bh != s_bar_h[i]) {
			lo = bh < s_bar_h[i] ? bh : s_bar_h[i];
			hi = bh < s_bar_h[i] ? s_bar_h[i] : bh;
			s_bar_h[i] = bh;
		}
		if (ch != s_cap_h[i]) {
			int32_t c_lo = ch < s_cap_h[i] ? ch : s_cap_h[i];
			int32_t c_hi = (ch < s_cap_h[i] ? s_cap_h[i] : ch) + CAP_HEIGHT_PX;
			lo = c_lo < lo ? c_lo : lo;
			hi = c_hi > hi ? c_hi : hi;
			s_cap_h[i] = ch;
		}
		if (hi > lo) {
			invalidate_rows(i, lo, hi);
		}
	}
}

void ui_spectrum_view_init(void)
{
	/* opaque page on top of the Now Playing tab, covering its full area
	 * including padding (see cover_whole_tab); stays clickable so taps
	 * don't reach the controls underneath */
	s_panel = lv_obj_create(ui_Now_Playing);
	lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(s_panel, lv_color_hex(BG_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(s_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_radius(s_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_width(s_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_pad_all(s_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_event_cb(s_panel, draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

	lv_obj_add_event_cb(ui_TabView1, tab_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
	lv_timer_create(refresh_timer_cb, SPECTRUM_REFRESH_MS, NULL);

	apply_setting();
}

void ui_spectrum_set_enabled(bool enabled)
{
	settings_set_spectrum(enabled);
	if (s_panel) {
		apply_setting();
	}
}
