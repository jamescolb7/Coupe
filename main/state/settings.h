#ifndef STATE_SETTINGS_H
#define STATE_SETTINGS_H

#include <stdbool.h>

void settings_init(void);

bool settings_get_spectrum(void);
void settings_set_spectrum(bool enabled);

#endif
