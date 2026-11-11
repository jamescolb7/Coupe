#include <stdint.h>

#include "esp_log.h"
#include "nvs.h"

#include "settings.h"

static const char *TAG = "SETTINGS";

#define SETTINGS_NAMESPACE "coupe"
#define KEY_SPECTRUM       "spectrum"

static bool s_spectrum;

static void save_u8(const char *key, uint8_t value)
{
	nvs_handle_t h;
	esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &h);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(err));
		return;
	}
	err = nvs_set_u8(h, key, value);
	if (err == ESP_OK) {
		err = nvs_commit(h);
	}
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "saving %s failed: %s", key, esp_err_to_name(err));
	}
	nvs_close(h);
}

void settings_init(void)
{
	nvs_handle_t h;
	/* namespace doesn't exist until the first save: keep defaults */
	if (nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
		return;
	}
	uint8_t v = 0;
	if (nvs_get_u8(h, KEY_SPECTRUM, &v) == ESP_OK) {
		s_spectrum = v != 0;
	}
	nvs_close(h);
}

bool settings_get_spectrum(void)
{
	return s_spectrum;
}

void settings_set_spectrum(bool enabled)
{
	if (s_spectrum == enabled) {
		return;
	}
	s_spectrum = enabled;
	save_u8(KEY_SPECTRUM, enabled ? 1 : 0);
}
