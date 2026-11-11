#ifndef MAIN_SCREEN_H
#define MAIN_SCREEN_H

#include "../ui.h"

extern lv_obj_t * ui_title_label;
extern lv_obj_t * ui_subtitle_label;
extern lv_obj_t * ui_album_image;
extern lv_obj_t * ui_progress_bar;
extern lv_obj_t * ui_connected_label;
extern lv_obj_t * ui_Label1; /* play/pause button icon */
extern lv_obj_t * ui_volumepanel;
extern lv_obj_t * ui_volumepercent;
extern lv_obj_t * ui_volumebar;

extern void ui_main_screen_init(void);
extern void ui_main_screen_destroy(void);

#endif