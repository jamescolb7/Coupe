#ifndef PLAYER_STATE_H
#define PLAYER_STATE_H

#include <stdbool.h>
#include <stdint.h>

#define PLAYER_TEXT_MAX        256
#define PLAYER_DEVICE_NAME_MAX 33

#define PLAYER_DIRTY_META     (1u << 0)
#define PLAYER_DIRTY_CONN     (1u << 1)
#define PLAYER_DIRTY_PROGRESS (1u << 2)
#define PLAYER_DIRTY_COVER    (1u << 3)
#define PLAYER_DIRTY_VOLUME   (1u << 4)

typedef struct {
	char title[PLAYER_TEXT_MAX];
	char artist[PLAYER_TEXT_MAX];
	char album[PLAYER_TEXT_MAX];

	bool connected;
	char device_name[PLAYER_DEVICE_NAME_MAX];

	uint32_t position_ms;
	uint32_t length_ms;
	int64_t position_stamp_us;
	bool playing;

	uint8_t volume_pct;

	/* ownership of cover_pixels passes to the snapshot taker (NULL if none) */
	uint16_t *cover_pixels;
	uint16_t cover_w;
	uint16_t cover_h;
} player_snapshot_t;

void player_state_init(void);

void player_set_title(const char *text, int len);
void player_set_artist(const char *text, int len);
void player_set_album(const char *text, int len);
void player_clear_metadata(void);
void player_set_connected(bool connected, const char *device_name /* nullable: keep current */);
void player_set_cover(uint16_t *pixels, uint16_t w, uint16_t h);
void player_set_volume(uint8_t avrc_volume);

//progress bar handling/player state
void player_set_progress(uint32_t position_ms, uint32_t length_ms, bool playing);
void player_set_playing(bool playing);
bool player_is_playing(void);
uint32_t player_position_at(const player_snapshot_t *s, int64_t now_us);


/* consumer (LVGL task): copies state into out, clears dirty bits, returns
 * the mask of what changed since the last call */
uint32_t player_take_snapshot(player_snapshot_t *out);

#endif
