#ifndef AUDIO_SPECTRUM_H
#define AUDIO_SPECTRUM_H

#include <stdbool.h>
#include <stdint.h>

#define SPECTRUM_BANDS 8

void spectrum_init(void);

/* A2DP data path: interleaved 16-bit PCM, len in bytes. Never blocks. */
void spectrum_feed(const uint8_t *pcm, uint32_t len);

/* stream format from ESP_A2D_AUDIO_CFG_EVT */
void spectrum_set_format(uint32_t sample_rate, uint8_t channels);

/* analysis only runs (and feed only copies) while active */
void spectrum_set_active(bool active);
bool spectrum_is_active(void);

/* latest levels/peak caps, 0-100 each */
void spectrum_get(uint8_t levels[SPECTRUM_BANDS], uint8_t peaks[SPECTRUM_BANDS]);

#endif
