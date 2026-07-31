#ifndef ANIMATIONS_H
#define ANIMATIONS_H

#include "lvgl.h"

extern lv_style_t anim_text_scroll_sync;

void anim_text_scroll_init(void);
void ui_marquee_resync(void);

#endif