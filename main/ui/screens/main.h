#ifndef MAIN_SCREEN_H
#define MAIN_SCREEN_H

#include "../ui.h"

extern lv_obj_t * ui_title_label;
extern lv_obj_t * ui_subtitle_label;
extern lv_obj_t * ui_album_image;
extern lv_obj_t * ui_progress_bar;

extern void ui_Screen1_screen_init(void);
extern void ui_Screen1_screen_destroy(void);

#endif