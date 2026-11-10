#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_lvgl_port.h"
#include "ui/ui_sync.h"
#include "ui/screens/main.h"
#include "ui/animations.h"
#include "state/player.h"

#define UI_SYNC_PERIOD_MS 50

//bluetooth connection indicators
#define ICON_CONNECTED    "\xEF\x80\x84" // U+F004
#define ICON_DISCONNECTED "\xEF\x80\x85" // U+F005

static player_snapshot_t s_snap;
static char s_subtitle[2 * PLAYER_TEXT_MAX + 8];

/* double-buffered image descriptors: the widget always points at a fully
 * valid descriptor; the old pixel buffer is freed only after the switch */
static lv_image_dsc_t s_cover_dsc[2];
static int s_cover_idx = -1;

// returns true if the label text actually changed
static bool label_set_if_changed(lv_obj_t *label, const char *text)
{
	const char *cur = lv_label_get_text(label);
	if (cur && strcmp(cur, text) == 0) {
		return false;
	}
	lv_label_set_text(label, text);
	return true;
}

static void apply_meta(const player_snapshot_t *s)
{
	bool title_empty = s->title[0] == '\0';
	bool artist_empty = s->artist[0] == '\0';
	bool album_empty = s->album[0] == '\0';

	if (artist_empty && album_empty) {
		if (s->connected) {
			snprintf(s_subtitle, sizeof(s_subtitle), "Connected to %s.", s->device_name[0] ? s->device_name : "Unknown");
		} else {
			snprintf(s_subtitle, sizeof(s_subtitle), "Now discoverable as Coupe.");
		}
	} else if (artist_empty || album_empty) {
		snprintf(s_subtitle, sizeof(s_subtitle), "%s", artist_empty ? s->album : s->artist);
	} else {
		snprintf(s_subtitle, sizeof(s_subtitle), "%s \xE2\x80\xA2 %s", s->artist, s->album);
	}

	bool changed = label_set_if_changed(ui_subtitle_label, s_subtitle);
	changed |= label_set_if_changed(ui_title_label, title_empty ? "Not Playing" : s->title);

	if (changed) {
		ui_marquee_resync();
	}
}

static void apply_conn(const player_snapshot_t *s)
{
	label_set_if_changed(ui_connected_label, s->connected ? ICON_CONNECTED : ICON_DISCONNECTED);
}

static void apply_progress(const player_snapshot_t *s)
{
	/* 0xFFFFFFFF = length not provided by the peer; also guard the int32 range */
	if (s->length_ms == 0 || s->length_ms > INT32_MAX) {
		lv_bar_set_range(ui_progress_bar, 0, 1);
		lv_bar_set_value(ui_progress_bar, 0, LV_ANIM_OFF);
		return;
	}

	uint32_t pos = s->position_ms > s->length_ms ? s->length_ms : s->position_ms;
	/* range first, so the new value isn't clamped against the old range */
	lv_bar_set_range(ui_progress_bar, 0, (int32_t)s->length_ms);
	lv_bar_set_value(ui_progress_bar, (int32_t)pos, LV_ANIM_ON);
}

static void apply_cover(player_snapshot_t *s)
{
	if (!s->cover_pixels) {
		return;
	}

	int next = (s_cover_idx + 1) & 1;
	lv_image_dsc_t *dsc = &s_cover_dsc[next];
	memset(dsc, 0, sizeof(*dsc));
	dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
	dsc->header.cf = LV_COLOR_FORMAT_RGB565;
	dsc->header.w = s->cover_w;
	dsc->header.h = s->cover_h;
	dsc->header.stride = s->cover_w * sizeof(uint16_t);
	dsc->data_size = (uint32_t)s->cover_w * s->cover_h * sizeof(uint16_t);
	dsc->data = (const uint8_t *)s->cover_pixels;

	lv_image_set_src(ui_album_image, dsc);

	/* widget no longer references the previous buffer */
	if (s_cover_idx >= 0) {
		free((void *)s_cover_dsc[s_cover_idx].data);
		s_cover_dsc[s_cover_idx].data = NULL;
	}
	s_cover_idx = next;
	s->cover_pixels = NULL; /* now owned by s_cover_dsc[next] */
}

static void ui_sync_timer_cb(lv_timer_t *timer)
{
	(void)timer;

	uint32_t dirty = player_take_snapshot(&s_snap);
	if (!dirty) {
		return;
	}

	if (dirty & PLAYER_DIRTY_CONN) {
		apply_conn(&s_snap);
	}
	if (dirty & PLAYER_DIRTY_META) {
		apply_meta(&s_snap);
	}
	if (dirty & PLAYER_DIRTY_PROGRESS) {
		apply_progress(&s_snap);
	}
	if (dirty & PLAYER_DIRTY_COVER) {
		apply_cover(&s_snap);
	}
}

void ui_sync_init(void)
{
	lv_timer_create(ui_sync_timer_cb, UI_SYNC_PERIOD_MS, NULL);
}
