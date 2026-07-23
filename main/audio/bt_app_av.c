/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "esp_log.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_avrc_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bt_app_core_utils.h"
#include "bt_app_av.h"
#include "bredr_app_common_utils.h"
#include "avrcp_utils_tags.h"
#include "avrcp_common_utils.h"
#include "avrcp_metadata_service.h"
#include "avrcp_metadata_utils.h"
#include "a2dp_utils_tags.h"
#include "a2dp_sink_common_utils.h"
#include "a2dp_sink_int_codec_utils.h"
#include "avrcp_cover_art_service.h"
#include "avrcp_cover_art_utils.h"

#include "esp_lvgl_port.h"

#include "ui/screens/main.h"

/* device name */
static const char local_device_name[] = "Coupé";

/* event for stack up */
enum {
    BT_APP_EVT_STACK_UP = 0,
};

/* AVRCP volume (0-127) is mapped to PCM gain through a dB-linear ("audio
 * taper") curve rather than straight linear scaling: human loudness
 * perception is roughly logarithmic, so a linear multiplier makes low
 * settings sound far louder than their numeric fraction suggests (e.g.
 * iOS's lowest non-zero step, ~7/127, was landing near max headphone
 * comfort). AVRC_VOLUME_MAX_DB also caps the loudest output below full
 * digital scale so the PCM5102A's analog stage isn't driven into
 * distortion at AVRCP volume 127. Both values were picked empirically on
 * this hardware -- if max volume still distorts, lower AVRC_VOLUME_MAX_DB
 * further (e.g. -9.0f); if the low end is too quiet or too loud, adjust
 * AVRC_VOLUME_MIN_DB. volume==0 always forces exact digital silence,
 * independent of this curve. */
#define AVRC_VOLUME_MAX_DB   (-6.0f)
#define AVRC_VOLUME_MIN_DB   (-50.0f)

/********************************
 * STATIC VARIABLE DEFINITIONS
 *******************************/

/* AVRCP absolute volume last received from the peer (0-127, 0x7f = 100%).
 * Defaults to max so audio isn't silently muted if the phone never sends
 * an initial SetAbsoluteVolume. Written only from bt_app_rc_tg_cb()
 * (Bluedroid callback context); read only from bt_app_a2d_data_cb()
 * (A2DP data callback context). volatile is sufficient: single-byte
 * load/store is atomic on ESP32, and one stale packet on a race is
 * harmless -- a mutex would just add latency to a per-packet hot path. */
static volatile uint8_t s_avrc_volume = 50;

/* reusable scratch buffer for volume-scaled PCM, grows via realloc only
 * when a larger packet than seen before arrives (stabilizes after the
 * first few packets of a connection since MTU is fixed for its lifetime) */
static uint8_t *s_vol_scratch_buf = NULL;
static uint32_t s_vol_scratch_cap = 0;

static char s_subtitle_artist[256] = {0};
static char s_subtitle_album[256] = {0};
static bool s_subtitle_artist_valid = false;
static bool s_subtitle_album_valid = false;

/********************************
 * STATIC FUNCTION DEFINITIONS
 *******************************/

static void bt_app_avrc_update_subtitle_label(void)
{
    if (!lvgl_port_lock(0)) {
        return;
    }

    if (s_subtitle_artist_valid && s_subtitle_album_valid) {
        lv_label_set_text_fmt(ui_subtitle_label, "%s • %s", s_subtitle_artist, s_subtitle_album);
    } 
    // else if (s_subtitle_artist_valid) {
    //     lv_label_set_text(ui_subtitle_label, s_subtitle_artist);
    // } else if (s_subtitle_album_valid) {
    //     lv_label_set_text(ui_subtitle_label, s_subtitle_album);
    // } else {
    //     lv_label_set_text(ui_subtitle_label, "");
    // }

    lvgl_port_unlock();
}

static void bt_app_avrc_clear_subtitle_metadata(void)
{
    s_subtitle_artist[0] = '\0';
    s_subtitle_album[0] = '\0';
    s_subtitle_artist_valid = false;
    s_subtitle_album_valid = false;
    bt_app_avrc_update_subtitle_label();
}

static void bt_app_avrc_request_play_status(void)
{
    esp_err_t err = esp_avrc_ct_send_get_play_status_cmd(bt_avrc_common_alloc_tl());
    if (err != ESP_OK) {
        ESP_LOGW(BT_RC_CT_TAG, "Failed to request play status: %s", esp_err_to_name(err));
    }
}

static void bt_app_avrc_ct_evt_hdl(uint16_t event, void *param)
{
    ESP_LOGD(BT_RC_CT_TAG, "%s event: %d", __func__, event);

    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(param);

    switch (event) {
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_PROF_STATE_EVT: {
        bt_avrc_common_ct_evt_def_hdl(event, param);
        break;
    }
    case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
        bt_avrc_md_ct_evt_hdl(event, param);
        bt_avrc_ca_ct_evt_hdl(event, param);
        if (rc->conn_stat.connected) {
            /* get remote supported event_ids of peer AVRCP Target */
            bt_avrc_common_ct_get_peer_rn_cap();
        } else {
            /* clear peer notification capability record */
            bt_avrc_common_ct_set_peer_rn_cap(0);
        }
        break;
    }
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
        if (rc->change_ntf.event_id == ESP_AVRC_RN_TRACK_CHANGE) {
            bt_app_avrc_clear_subtitle_metadata();
        }
        bt_avrc_md_ct_evt_hdl(event, param);
        bt_avrc_ca_ct_evt_hdl(event, param);
        if (rc->change_ntf.event_id == ESP_AVRC_RN_TRACK_CHANGE ||
            rc->change_ntf.event_id == ESP_AVRC_RN_PLAY_POS_CHANGED ||
            rc->change_ntf.event_id == ESP_AVRC_RN_PLAY_STATUS_CHANGE) {
            bt_app_avrc_request_play_status();
        }
        bt_avrc_common_ct_notify_evt_handler(rc->change_ntf.event_id, &rc->change_ntf.event_parameter);
        break;
    }
    case ESP_AVRC_CT_PLAY_STATUS_RSP_EVT: {
        ESP_LOGI(BT_RC_CT_TAG, "AVRC play status: current=%" PRIu32 "s length=%" PRIu32 "s",
                 rc->play_status_rsp.song_position / 1000,
                 rc->play_status_rsp.song_length / 1000);

         if (lvgl_port_lock(0)) {
            lv_bar_set_value(ui_progress_bar, rc->play_status_rsp.song_position, LV_ANIM_ON);
            lv_bar_set_max_value(ui_progress_bar, rc->play_status_rsp.song_length);
            lvgl_port_unlock();
        }

        bt_avrc_common_ct_evt_def_hdl(event, param);
        break;
    }
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        /* set peer notification capability record */
        bt_avrc_common_ct_set_peer_rn_cap(rc->get_rn_caps_rsp.evt_set.bits);
        bt_avrc_common_ct_rn_track_changed();
        bt_avrc_common_ct_rn_play_status_changed();
        bt_avrc_common_ct_rn_play_pos_changed();

        bt_app_avrc_request_play_status();

        bt_avrc_md_ct_evt_hdl(event, param);
        bt_avrc_ca_ct_evt_hdl(event, param);
        break;
    }
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        if (rc->meta_rsp.attr_text) {
            if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_TITLE) {
                if (lvgl_port_lock(0)) {
                    lv_label_set_text_fmt(ui_title_label, "%.*s", (int)rc->meta_rsp.attr_length, (const char *)rc->meta_rsp.attr_text);
                    lvgl_port_unlock();
                }
                ESP_LOGI(BT_RC_CT_TAG, "AVRC metadata rsp: song title: %.*s", (int)rc->meta_rsp.attr_length, rc->meta_rsp.attr_text);
            } else if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_ARTIST) {
                snprintf(s_subtitle_artist, sizeof(s_subtitle_artist), "%.*s", (int)rc->meta_rsp.attr_length, (const char *)rc->meta_rsp.attr_text);
                s_subtitle_artist_valid = true;
                bt_app_avrc_update_subtitle_label();
                ESP_LOGI(BT_RC_CT_TAG, "AVRC metadata rsp: artist: %.*s", (int)rc->meta_rsp.attr_length, rc->meta_rsp.attr_text);
            } else if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_ALBUM) {
                snprintf(s_subtitle_album, sizeof(s_subtitle_album), "%.*s", (int)rc->meta_rsp.attr_length, (const char *)rc->meta_rsp.attr_text);
                s_subtitle_album_valid = true;
                bt_app_avrc_update_subtitle_label();
                ESP_LOGI(BT_RC_CT_TAG, "AVRC metadata rsp: album: %.*s", (int)rc->meta_rsp.attr_length, rc->meta_rsp.attr_text);
            }
        }
        
        if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_COVER_ART) {
            bt_avrc_ca_ct_evt_hdl(event, param);
        } else {
            bt_avrc_md_ct_evt_hdl(event, param);
        }
        break;
    }
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
        bt_avrc_ca_ct_evt_hdl(event, param);
        break;
    }
    case ESP_AVRC_CT_COVER_ART_STATE_EVT:
    case ESP_AVRC_CT_COVER_ART_DATA_EVT: {
        bt_avrc_ca_ct_evt_hdl(event, param);
        break;
    }
    default:
        ESP_LOGE(BT_RC_CT_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

/* map AVRCP volume (0-127) to a linear PCM gain factor via the dB-linear
 * taper described above; volume 0 is an exact-zero special case (true
 * digital silence, not just "very quiet") */
static float bt_app_avrc_volume_to_gain(uint8_t volume)
{
    if (volume == 0) {
        return 0.0f;
    }

    float frac = (float)volume / 0x7f;
    float db = AVRC_VOLUME_MIN_DB + frac * (AVRC_VOLUME_MAX_DB - AVRC_VOLUME_MIN_DB);
    return powf(10.0f, db / 20.0f);
}

/* scale 16-bit signed interleaved PCM by gain (linear factor from
 * bt_app_avrc_volume_to_gain) and forward it. data is int16_t samples:
 * with internal SBC decode (i.e. CONFIG_EXAMPLE_A2DP_SINK_USE_EXTERNAL_CODEC
 * unset), Bluedroid always hands us standard interleaved 16-bit signed PCM
 * and len is always even. */
static void bt_app_a2d_scale_and_forward(const uint8_t *data, uint32_t len, float gain)
{
    if (len > s_vol_scratch_cap) {
        uint8_t *new_buf = realloc(s_vol_scratch_buf, len);
        if (new_buf == NULL) {
            ESP_LOGW(BT_AV_TAG, "volume scratch buf alloc failed (%" PRIu32 " bytes), passing through unscaled", len);
            bt_a2d_data_hdl(data, len);
            return;
        }
        s_vol_scratch_buf = new_buf;
        s_vol_scratch_cap = len;
    }

    if (gain <= 0.0f) {
        memset(s_vol_scratch_buf, 0, len);
        bt_a2d_data_hdl(s_vol_scratch_buf, len);
        return;
    }

    const int16_t *src = (const int16_t *)data;
    int16_t *dst = (int16_t *)s_vol_scratch_buf;
    uint32_t sample_count = len / sizeof(int16_t);

    for (uint32_t i = 0; i < sample_count; i++) {
        /* gain is always < 1.0 given AVRC_VOLUME_MAX_DB <= 0, but clamp
         * defensively in case the tunable constants above are ever set
         * to provide positive gain (boost) */
        int32_t sample = (int32_t)((float)src[i] * gain);
        if (sample > INT16_MAX) {
            sample = INT16_MAX;
        } else if (sample < INT16_MIN) {
            sample = INT16_MIN;
        }
        dst[i] = (int16_t)sample;
    }

    bt_a2d_data_hdl(s_vol_scratch_buf, len);
}

/* the AVRCP spec requires a TG to send an INTERIM response to a peer's
 * ESP_AVRC_RN_VOLUME_CHANGE registration before that peer will treat
 * absolute volume as supported and start sending SetAbsoluteVolume
 * commands; without this response the peer (e.g. Android) silently
 * falls back to local-only volume handling and never syncs volume here */
static void bt_app_avrc_tg_reg_ntf_hdl(uint16_t event, void *param)
{
    esp_avrc_tg_cb_param_t *rc = (esp_avrc_tg_cb_param_t *)param;

    bt_avrc_common_tg_evt_def_hdl(event, param);

    if (rc->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
        esp_avrc_rn_param_t rn_param = { .volume = s_avrc_volume };
        esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &rn_param);
    }
}

/********************************
 * EXTERNAL FUNCTION DEFINITIONS
 *******************************/

void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_PROF_STATE_EVT:
    case ESP_A2D_SNK_PSC_CFG_EVT:
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT: {
        bt_app_work_dispatch(bt_a2d_evt_def_hdl, event, param, sizeof(esp_a2d_cb_param_t), NULL, NULL);
        break;
    }
    case ESP_A2D_CONNECTION_STATE_EVT:
    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT:
    case ESP_A2D_SEP_REG_STATE_EVT: {
        bt_app_work_dispatch(bt_a2d_evt_int_codec_hdl, event, param, sizeof(esp_a2d_cb_param_t), NULL, NULL);
        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "Invalid A2DP event: %d", event);
        break;
    }
}

void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    bt_app_a2d_scale_and_forward(data, len, bt_app_avrc_volume_to_gain(s_avrc_volume));
}

void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        bt_app_work_dispatch(bt_app_avrc_ct_evt_hdl, event, param, sizeof(esp_avrc_ct_cb_param_t),
                             bt_avrc_common_copy_metadata, bt_avrc_common_free_metadata);
        break;
    }
    case ESP_AVRC_CT_COVER_ART_DATA_EVT: {
        /* we must handle ESP_AVRC_CT_COVER_ART_DATA_EVT in avrcp controller callback, */
        /* copy image data to other buff before return if need */
        if (param->cover_art_data.status == ESP_BT_STATUS_SUCCESS) {
            avrc_cover_art_srv_save_image_data(param->cover_art_data.p_data, param->cover_art_data.data_len);
        } else {
            ESP_LOGE(BT_RC_CT_TAG, "Cover Art Client get operation failed");
            break;
        }
        bt_app_work_dispatch(bt_app_avrc_ct_evt_hdl, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL, NULL);
        break;
    }
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
    case ESP_AVRC_CT_PLAY_STATUS_RSP_EVT:
    case ESP_AVRC_CT_COVER_ART_STATE_EVT:
    case ESP_AVRC_CT_PROF_STATE_EVT:
        bt_app_work_dispatch(bt_app_avrc_ct_evt_hdl, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL, NULL);
        break;
    default:
        ESP_LOGE(BT_RC_CT_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
        /* runs synchronously on the Bluedroid callback context, before
         * bt_app_work_dispatch() below runs asynchronously -- safe/immediate */
        s_avrc_volume = param->set_abs_vol.volume;
        ESP_LOGI(BT_RC_TG_TAG, "AVRCP absolute volume set to %u/127", (unsigned)s_avrc_volume);
        bt_app_work_dispatch(bt_avrc_common_tg_evt_def_hdl, event, param, sizeof(esp_avrc_tg_cb_param_t), NULL, NULL);
        break;
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
        bt_app_work_dispatch(bt_app_avrc_tg_reg_ntf_hdl, event, param, sizeof(esp_avrc_tg_cb_param_t), NULL, NULL);
        break;
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
    case ESP_AVRC_TG_REMOTE_FEATURES_EVT:
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:
    case ESP_AVRC_TG_SET_PLAYER_APP_VALUE_EVT:
    case ESP_AVRC_TG_PROF_STATE_EVT:
        bt_app_work_dispatch(bt_avrc_common_tg_evt_def_hdl, event, param, sizeof(esp_avrc_tg_cb_param_t), NULL, NULL);
        break;
    default:
        ESP_LOGE(BT_RC_TG_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

static void bt_app_dev_cb(esp_bt_dev_cb_event_t event, esp_bt_dev_cb_param_t *param)
{
    bredr_app_dev_evt_def_hdl(event, param);
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    bredr_app_gap_evt_def_hdl(event, param);
}

static void bt_av_hdl_stack_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_AV_TAG, "%s event: %d", __func__, event);

    switch (event) {
    /* when do the stack up, this event comes */
    case BT_APP_EVT_STACK_UP: {
        esp_bt_gap_set_device_name(local_device_name);
        esp_bt_dev_register_callback(bt_app_dev_cb);
        esp_bt_gap_register_callback(bt_app_gap_cb);

        esp_avrc_ct_register_callback(bt_app_rc_ct_cb);
        assert(esp_avrc_ct_init() == ESP_OK);
        esp_avrc_tg_register_callback(bt_app_rc_tg_cb);
        assert(esp_avrc_tg_init() == ESP_OK);

        /* declare support for AVRCP absolute volume: without this, peers
         * never learn this device can be volume-synced and fall back to
         * local-only volume handling, so SetAbsoluteVolume is never sent */
        esp_avrc_rn_evt_cap_mask_t evt_set = {0};
        esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
        assert(esp_avrc_tg_set_rn_evt_cap(&evt_set) == ESP_OK);

        esp_a2d_register_callback(&bt_app_a2d_cb);
        assert(esp_a2d_sink_init() == ESP_OK);

        esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);

        /* Get the default value of the delay value */
        esp_a2d_sink_get_delay_value();
        /* Get local device name */
        esp_bt_gap_get_device_name();

        /* set discoverable and connectable mode, wait to be connected */
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        break;
    }
    /* others */
    default:
        ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
}

void bt_app_init(void) {
    ESP_ERROR_CHECK(bredr_app_common_init());
    bt_app_task_start_up();
    /* bluetooth device name, connection mode and profile set up */
    bt_app_work_dispatch(bt_av_hdl_stack_evt, BT_APP_EVT_STACK_UP, NULL, 0, NULL, NULL);
}
