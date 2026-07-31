#ifndef PLAYER_STATE_H
#define PLAYER_STATE_H

#include <stdbool.h>

extern char playing_title[256];
extern char playing_album[256];
extern char playing_artist[256];

// extern uint8_t *player_cover_data;
// extern uint32_t player_cover_size;

extern bool bt_connected;
extern char connected_device_name[33];

extern bool playing_title_valid;
extern bool playing_album_valid;
extern bool playing_artist_valid;

extern bool connected_device_name_valid;

void player_update_metadata(void);
void player_clear_metadata(void);
void player_connection_state(void);

#endif