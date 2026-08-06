#include "esp_err.h"
#include "esp_lvgl_port.h"

#include "player.h"
#include "ui/screens/main.h"
#include "ui/animations.h"

char playing_title[256] = {0};
char playing_album[256] = {0};
char playing_artist[256] = {0};

bool bt_connected = false;
char connected_device_name[33] = {0};

// uint8_t *player_cover_data;
// uint32_t player_cover_size;

bool playing_title_valid = false;
bool playing_album_valid = false;
bool playing_artist_valid = false;

bool connected_device_name_valid = false;
// bool player_cover_valid = false;

void player_update_metadata(void) {
	if (!playing_title_valid || !playing_album_valid || !playing_artist_valid) return;

	if (lvgl_port_lock(0)) {
		// player_cover_valid = false;

		bool playing_title_empty = playing_title[0] == '\0';
		bool playing_artist_empty = playing_artist[0] == '\0';
		bool playing_album_empty = playing_artist[0] == '\0';
		bool connected_device_name_empty = connected_device_name[0] == '\0';

		if (playing_artist_empty && playing_album_empty) {
			lv_label_set_text_fmt(ui_subtitle_label, "Connected to %s.", connected_device_name_empty ? "Unknown" : connected_device_name);
		} else if (playing_artist_empty || playing_album_empty) {
			lv_label_set_text_fmt(ui_subtitle_label, "%s", playing_artist_empty ? playing_album : playing_artist);
		} else {
			lv_label_set_text_fmt(ui_subtitle_label, "%s • %s", playing_artist, playing_album);
		}

		lv_label_set_text_fmt(ui_title_label, "%s", playing_title_empty ? "Not Playing" : playing_title);
		ui_marquee_resync();
	}

	playing_title_valid = false;
	playing_album_valid = false;
	playing_artist_valid = false;

	lvgl_port_unlock();
}

void player_clear_metadata(void) {
	playing_title[0] = '\0';
	playing_album[0] = '\0';
	playing_artist[0] = '\0';

	playing_title_valid = true;
	playing_artist_valid = true;
	playing_album_valid = true;

	player_update_metadata();
}

void player_connection_state(void) {
	if (lvgl_port_lock(0)) {
		lv_label_set_text_fmt(ui_connected_label, "%s", bt_connected ? "" : "");
	}

	lvgl_port_unlock();
}