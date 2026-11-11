#ifndef UI_SPECTRUM_VIEW_H
#define UI_SPECTRUM_VIEW_H

#include <stdbool.h>

/* LVGL task only. Builds the spectrum page over the Now Playing tab and
 * applies the saved setting; call after ui_main_screen_init(). */
void ui_spectrum_view_init(void);

/* settings switch: persist and show/hide the spectrum page */
void ui_spectrum_set_enabled(bool enabled);

#endif
