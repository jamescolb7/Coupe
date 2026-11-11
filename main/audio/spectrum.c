#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_dsp.h"

#include "spectrum.h"

static const char *TAG = "SPECTRUM";

/* FFT size: 1024 samples (~43 Hz per bin at 44.1 kHz), 50% overlap: a new
 * spectrum every FFT_HOP samples = ~11.6 ms (~86 frames/s) */
#define FFT_N                 1024
#define FFT_HOP               (FFT_N / 2)
#define FEED_CHUNK_FRAMES     256
#define STREAM_SAMPLES        4096

/* band layout: SPECTRUM_BANDS log-spaced bands between these edges */
#define BAND_F_MIN_HZ         50.0f
#define BAND_F_MAX_HZ         16000.0f

/* display range and look */
#define LEVEL_FLOOR_DB        (-60.0f)   /* maps to 0 */
#define LEVEL_CEIL_DB         (0.0f)     /* maps to 100 */
/* music has less energy up high; tilt so the treble bands are visible
 * (+dB per octave relative to 1 kHz) */
#define TILT_DB_PER_OCTAVE    3.0f
/* per analysis frame (~11.6 ms) */
#define LEVEL_FALL_PER_FRAME  2.5f       /* bar release, units of 0-100: full fall ~0.45 s */
#define PEAK_HOLD_FRAMES      26         /* ~300 ms */
#define PEAK_FALL_PER_FRAME   0.8f

/* the PCM fed here is ~110-185 ms ahead of the speaker (A2DP ringbuffer
 * before I2S); delay what is shown so the bars line up with what's heard */
#define SPECTRUM_DISPLAY_DELAY_MS 130
#define DELAY_RING_FRAMES     32         /* > delay / hop (130 ms / 11.6 ms) */

/* no samples for this long = paused/stopped: let the bars fall */
#define SILENCE_TIMEOUT_MS    100

/* ---- shared state ---- */
static volatile bool s_active;
static volatile uint32_t s_sample_rate = 44100;
static volatile uint8_t s_channels = 2;

static StreamBufferHandle_t s_stream;
static portMUX_TYPE s_out_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_out_levels[SPECTRUM_BANDS];
static uint8_t s_out_peaks[SPECTRUM_BANDS];

/* ---- feed side (A2DP data task only) ---- */
static int16_t s_feed_buf[FEED_CHUNK_FRAMES];

/* ---- analysis task only ---- */
static float *s_fft;      /* FFT_N complex: re, im interleaved */
static float *s_window;   /* FFT_N */
static int16_t *s_frame;  /* FFT_N mono samples */

static uint16_t s_band_lo[SPECTRUM_BANDS];
static uint16_t s_band_hi[SPECTRUM_BANDS]; /* exclusive */
static float s_band_tilt_db[SPECTRUM_BANDS];
static uint32_t s_layout_rate;

static float s_level[SPECTRUM_BANDS];
static float s_peak[SPECTRUM_BANDS];
static uint8_t s_peak_hold[SPECTRUM_BANDS];

static uint8_t s_ring_levels[DELAY_RING_FRAMES][SPECTRUM_BANDS];
static uint8_t s_ring_peaks[DELAY_RING_FRAMES][SPECTRUM_BANDS];
static uint32_t s_ring_head;

static void *alloc_fast(size_t size)
{
	/* FFT is much faster from internal RAM; fall back to PSRAM if tight */
	void *p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	if (!p) {
		p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
	}
	return p;
}

static void compute_band_layout(uint32_t rate)
{
	uint16_t edges[SPECTRUM_BANDS + 1];
	const uint16_t max_bin = FFT_N / 2;
	const float ratio = BAND_F_MAX_HZ / BAND_F_MIN_HZ;

	for (int b = 0; b <= SPECTRUM_BANDS; b++) {
		float f = BAND_F_MIN_HZ * powf(ratio, (float)b / SPECTRUM_BANDS);
		int bin = (int)(f * FFT_N / rate + 0.5f);
		if (bin < 1) {
			bin = 1; /* skip DC */
		}
		/* strictly increasing: every (bass) band gets its own bin */
		if (b > 0 && bin <= edges[b - 1]) {
			bin = edges[b - 1] + 1;
		}
		if (bin > max_bin) {
			bin = max_bin;
		}
		edges[b] = (uint16_t)bin;
	}

	for (int b = 0; b < SPECTRUM_BANDS; b++) {
		s_band_lo[b] = edges[b];
		s_band_hi[b] = edges[b + 1] > edges[b] ? edges[b + 1] : edges[b] + 1;
		if (s_band_hi[b] > max_bin) {
			s_band_hi[b] = max_bin;
		}
		float fc = sqrtf((float)s_band_lo[b] * (float)s_band_hi[b]) * rate / FFT_N;
		s_band_tilt_db[b] = TILT_DB_PER_OCTAVE * log2f(fc / 1000.0f);
	}
	s_layout_rate = rate;
}

static void reset_levels(void)
{
	memset(s_level, 0, sizeof(s_level));
	memset(s_peak, 0, sizeof(s_peak));
	memset(s_peak_hold, 0, sizeof(s_peak_hold));
	memset(s_ring_levels, 0, sizeof(s_ring_levels));
	memset(s_ring_peaks, 0, sizeof(s_ring_peaks));
	portENTER_CRITICAL(&s_out_mux);
	memset(s_out_levels, 0, sizeof(s_out_levels));
	memset(s_out_peaks, 0, sizeof(s_out_peaks));
	portEXIT_CRITICAL(&s_out_mux);
}

/* raw[b] = new level 0-100 (or NULL for a silent frame) */
static void push_frame(const float *raw, uint32_t rate)
{
	for (int b = 0; b < SPECTRUM_BANDS; b++) {
		float target = raw ? raw[b] : 0.0f;
		float fallen = s_level[b] - LEVEL_FALL_PER_FRAME;
		s_level[b] = target > fallen ? target : (fallen > 0 ? fallen : 0);

		if (s_level[b] >= s_peak[b]) {
			s_peak[b] = s_level[b];
			s_peak_hold[b] = PEAK_HOLD_FRAMES;
		} else if (s_peak_hold[b] > 0) {
			s_peak_hold[b]--;
		} else {
			s_peak[b] -= PEAK_FALL_PER_FRAME;
			if (s_peak[b] < s_level[b]) {
				s_peak[b] = s_level[b];
			}
		}
	}

	/* delay line: write this frame, publish the one from DELAY ago */
	uint32_t w = s_ring_head % DELAY_RING_FRAMES;
	for (int b = 0; b < SPECTRUM_BANDS; b++) {
		s_ring_levels[w][b] = (uint8_t)(s_level[b] + 0.5f);
		s_ring_peaks[w][b] = (uint8_t)(s_peak[b] + 0.5f);
	}

	/* one frame per hop; in 0.1 ms units to keep precision at ~11.6 ms */
	uint32_t frame_100us = (uint32_t)((uint64_t)FFT_HOP * 10000 / (rate ? rate : 44100));
	uint32_t delay = frame_100us ? (SPECTRUM_DISPLAY_DELAY_MS * 10 + frame_100us / 2) / frame_100us : 0;
	if (delay >= DELAY_RING_FRAMES) {
		delay = DELAY_RING_FRAMES - 1;
	}
	uint32_t r = (s_ring_head + DELAY_RING_FRAMES - delay) % DELAY_RING_FRAMES;
	s_ring_head++;

	portENTER_CRITICAL(&s_out_mux);
	memcpy(s_out_levels, s_ring_levels[r], SPECTRUM_BANDS);
	memcpy(s_out_peaks, s_ring_peaks[r], SPECTRUM_BANDS);
	portEXIT_CRITICAL(&s_out_mux);
}

static void analyze_frame(uint32_t rate)
{
	static float raw[SPECTRUM_BANDS];

	for (int i = 0; i < FFT_N; i++) {
		s_fft[2 * i] = (s_frame[i] * (1.0f / 32768.0f)) * s_window[i];
		s_fft[2 * i + 1] = 0.0f;
	}
	dsps_fft2r_fc32(s_fft, FFT_N);
	dsps_bit_rev_fc32(s_fft, FFT_N);

	/* full-scale sine through a Hann window peaks at N/4 */
	const float ref_db = 20.0f * log10f(FFT_N / 4.0f);

	for (int b = 0; b < SPECTRUM_BANDS; b++) {
		float max_mag2 = 0.0f;
		for (int k = s_band_lo[b]; k < s_band_hi[b]; k++) {
			float re = s_fft[2 * k];
			float im = s_fft[2 * k + 1];
			float mag2 = re * re + im * im;
			if (mag2 > max_mag2) {
				max_mag2 = mag2;
			}
		}
		float db = 10.0f * log10f(max_mag2 + 1e-12f) - ref_db + s_band_tilt_db[b];
		float v = (db - LEVEL_FLOOR_DB) * (100.0f / (LEVEL_CEIL_DB - LEVEL_FLOOR_DB));
		raw[b] = v < 0 ? 0 : (v > 100 ? 100 : v);
	}

	push_frame(raw, rate);
}

static void spectrum_task(void *arg)
{
	(void)arg;
	bool was_active = false;
	size_t have = 0; /* samples in s_frame */
	TickType_t last_data = xTaskGetTickCount();

	for (;;) {
		if (!s_active) {
			if (was_active) {
				was_active = false;
				reset_levels();
			}
			vTaskDelay(pdMS_TO_TICKS(50));
			continue;
		}
		if (!was_active) {
			/* drop stale audio from before activation (drain rather than
			 * xStreamBufferReset: the feed side may be writing concurrently) */
			while (xStreamBufferReceive(s_stream, (uint8_t *)s_frame, FFT_N * sizeof(int16_t), 0) > 0) {
			}
			reset_levels();
			have = 0;
			last_data = xTaskGetTickCount();
			was_active = true;
		}

		uint32_t rate = s_sample_rate;
		if (rate != s_layout_rate) {
			compute_band_layout(rate);
		}

		size_t got = xStreamBufferReceive(s_stream, (uint8_t *)&s_frame[have],
		                                  (FFT_N - have) * sizeof(int16_t), pdMS_TO_TICKS(25));
		if (got > 0) {
			have += got / sizeof(int16_t);
			last_data = xTaskGetTickCount();
			if (have >= FFT_N) {
				analyze_frame(rate);
				/* 50% overlap: keep the newest half as the next frame's start */
				memmove(s_frame, &s_frame[FFT_HOP], (FFT_N - FFT_HOP) * sizeof(int16_t));
				have = FFT_N - FFT_HOP;
			}
		} else if ((xTaskGetTickCount() - last_data) > pdMS_TO_TICKS(SILENCE_TIMEOUT_MS)) {
			/* paused/stopped: keep producing frames so the bars fall */
			push_frame(NULL, rate);
		}
	}
}

void spectrum_init(void)
{
	s_fft = alloc_fast(FFT_N * 2 * sizeof(float));
	s_window = alloc_fast(FFT_N * sizeof(float));
	s_frame = alloc_fast(FFT_N * sizeof(int16_t));
	s_stream = xStreamBufferCreate(STREAM_SAMPLES * sizeof(int16_t), sizeof(int16_t));
	if (!s_fft || !s_window || !s_frame || !s_stream) {
		ESP_LOGE(TAG, "out of memory, spectrum analyzer disabled");
		return;
	}

	esp_err_t err = dsps_fft2r_init_fc32(NULL, FFT_N);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "FFT init failed: %s", esp_err_to_name(err));
		return;
	}
	dsps_wind_hann_f32(s_window, FFT_N);
	compute_band_layout(s_sample_rate);

	/* core 1 with the UI, below the LVGL task (prio 4): never competes
	 * with the BT stack / audio path on core 0 */
	xTaskCreatePinnedToCore(spectrum_task, "spectrum", 4096, NULL, 2, NULL, 1);
}

void spectrum_feed(const uint8_t *pcm, uint32_t len)
{
	if (!s_active || !s_stream) {
		return;
	}

	const int16_t *in = (const int16_t *)pcm;
	const uint8_t ch = s_channels;
	uint32_t frames = len / (sizeof(int16_t) * ch);

	while (frames > 0) {
		uint32_t n = frames > FEED_CHUNK_FRAMES ? FEED_CHUNK_FRAMES : frames;
		if (ch >= 2) {
			for (uint32_t i = 0; i < n; i++) {
				s_feed_buf[i] = (int16_t)(((int32_t)in[2 * i] + in[2 * i + 1]) / 2);
			}
		} else {
			memcpy(s_feed_buf, in, n * sizeof(int16_t));
		}
		/* never block the audio path: drop if the analyzer is behind */
		xStreamBufferSend(s_stream, s_feed_buf, n * sizeof(int16_t), 0);
		in += n * ch;
		frames -= n;
	}
}

void spectrum_set_format(uint32_t sample_rate, uint8_t channels)
{
	if (sample_rate) {
		s_sample_rate = sample_rate;
	}
	if (channels) {
		s_channels = channels;
	}
}

void spectrum_set_active(bool active)
{
	s_active = active && s_stream && s_fft;
}

bool spectrum_is_active(void)
{
	return s_active;
}

void spectrum_get(uint8_t levels[SPECTRUM_BANDS], uint8_t peaks[SPECTRUM_BANDS])
{
	portENTER_CRITICAL(&s_out_mux);
	memcpy(levels, s_out_levels, SPECTRUM_BANDS);
	memcpy(peaks, s_out_peaks, SPECTRUM_BANDS);
	portEXIT_CRITICAL(&s_out_mux);
}
