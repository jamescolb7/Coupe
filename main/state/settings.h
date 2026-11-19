#ifndef STATE_SETTINGS_H
#define STATE_SETTINGS_H

#include <stdbool.h>

void settings_init(void);

bool settings_get_spectrum(void);
void settings_set_spectrum(bool enabled);

bool settings_get_flip_vertical(void);
void settings_set_flip_vertical(bool flip);

#endif
