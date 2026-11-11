#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#include "player.h"

static SemaphoreHandle_t s_mutex;
static player_snapshot_t s_state;
static uint32_t s_dirty;

static bool s_title_valid;
static bool s_artist_valid;
static bool s_album_valid;

static void lock(void)
{
	xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void unlock(void)
{
	xSemaphoreGive(s_mutex);
}

static void copy_text(char *dst, size_t dst_size, const char *text, int len)
{
	if (!text || len <= 0) {
		dst[0] = '\0';
		return;
	}
	snprintf(dst, dst_size, "%.*s", len, text);
}

static void publish_meta_if_complete(void)
{
	if (s_title_valid && s_artist_valid && s_album_valid) {
		s_dirty |= PLAYER_DIRTY_META;
		s_title_valid = false;
		s_artist_valid = false;
		s_album_valid = false;
	}
}

void player_state_init(void)
{
	if (!s_mutex) {
		s_mutex = xSemaphoreCreateMutex();
		configASSERT(s_mutex);
	}
}

void player_set_title(const char *text, int len)
{
	lock();
	copy_text(s_state.title, sizeof(s_state.title), text, len);
	s_title_valid = true;
	publish_meta_if_complete();
	unlock();
}

void player_set_artist(const char *text, int len)
{
	lock();
	copy_text(s_state.artist, sizeof(s_state.artist), text, len);
	s_artist_valid = true;
	publish_meta_if_complete();
	unlock();
}

void player_set_album(const char *text, int len)
{
	lock();
	copy_text(s_state.album, sizeof(s_state.album), text, len);
	s_album_valid = true;
	publish_meta_if_complete();
	unlock();
}

void player_clear_metadata(void)
{
	lock();
	s_state.title[0] = '\0';
	s_state.artist[0] = '\0';
	s_state.album[0] = '\0';
	s_title_valid = false;
	s_artist_valid = false;
	s_album_valid = false;
	//new track starts from 0 until the peer reports otherwise
	s_state.position_ms = 0;
	s_state.position_stamp_us = esp_timer_get_time();
	s_dirty |= PLAYER_DIRTY_META | PLAYER_DIRTY_PROGRESS;
	unlock();
}

void player_set_connected(bool connected, const char *device_name)
{
	lock();
	s_state.connected = connected;
	if (!connected) {
		s_state.device_name[0] = '\0';
	} else if (device_name) {
		snprintf(s_state.device_name, sizeof(s_state.device_name), "%s", device_name);
	}
	//whenever there is no metadata/data, show device name/not playing
	s_dirty |= PLAYER_DIRTY_CONN | PLAYER_DIRTY_META;
	unlock();
}

uint32_t player_position_at(const player_snapshot_t *s, int64_t now_us)
{
	if (!s->playing || now_us <= s->position_stamp_us) {
		return s->position_ms;
	}
	uint64_t pos = (uint64_t)s->position_ms + (uint64_t)(now_us - s->position_stamp_us) / 1000;
	/* 0xFFFFFFFF = length unknown: don't clamp to it */
	if (s->length_ms != 0 && s->length_ms != UINT32_MAX && pos > s->length_ms) {
		pos = s->length_ms;
	}
	return pos > UINT32_MAX ? UINT32_MAX : (uint32_t)pos;
}

void player_set_progress(uint32_t position_ms, uint32_t length_ms, bool playing)
{
	lock();
	s_state.position_ms = position_ms;
	s_state.length_ms = length_ms;
	s_state.position_stamp_us = esp_timer_get_time();
	s_state.playing = playing;
	s_dirty |= PLAYER_DIRTY_PROGRESS;
	unlock();
}

void player_set_playing(bool playing)
{
	lock();
	if (s_state.playing != playing) {
		int64_t now = esp_timer_get_time();
		/* re-anchor at the current extrapolated position, so pausing
		 * freezes the bar where it is and resuming continues from there */
		s_state.position_ms = player_position_at(&s_state, now);
		s_state.position_stamp_us = now;
		s_state.playing = playing;
		s_dirty |= PLAYER_DIRTY_PROGRESS;
	}
	unlock();
}

void player_set_volume(uint8_t avrc_volume)
{
	if (avrc_volume > 0x7f) {
		avrc_volume = 0x7f;
	}
	lock();
	//take 0-127 into 0-100
	s_state.volume_pct = (uint8_t)(((uint32_t)avrc_volume * 100 + 63) / 127);
	s_dirty |= PLAYER_DIRTY_VOLUME;
	unlock();
}

bool player_is_playing(void)
{
	lock();
	bool playing = s_state.playing;
	unlock();
	return playing;
}

void player_set_cover(uint16_t *pixels, uint16_t w, uint16_t h)
{
	uint16_t *superseded = NULL;

	lock();
	superseded = s_state.cover_pixels;
	s_state.cover_pixels = pixels;
	s_state.cover_w = w;
	s_state.cover_h = h;
	s_dirty |= PLAYER_DIRTY_COVER;
	unlock();

	free(superseded);
}

uint32_t player_take_snapshot(player_snapshot_t *out)
{
	lock();
	uint32_t dirty = s_dirty;
	if (dirty) {
		memcpy(out, &s_state, sizeof(*out));
		s_state.cover_pixels = NULL;
		s_dirty = 0;
	}
	unlock();

	if (!(dirty & PLAYER_DIRTY_COVER)) {
		out->cover_pixels = NULL;
	}
	return dirty;
}
