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
#include "esp_timer.h"

#include "bt_app_core_utils.h"
#include "sink.h"
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

#include "state/player.h"

/* device name */
static const char local_device_name[] = "Coupé";

/* event for stack up */
enum {
    BT_APP_EVT_STACK_UP = 0,
};

#define AVRC_VOLUME_MAX_DB   (-8.0f)
#define AVRC_VOLUME_MIN_DB   (-50.0f)

static volatile uint8_t s_avrc_volume = 50;

static uint8_t *s_vol_scratch_buf = NULL;
static uint32_t s_vol_scratch_cap = 0;

//resync period for updating current playback status
#define PLAY_STATUS_RESYNC_PERIOD_US (5 * 1000 * 1000)

static volatile bool s_avrc_ct_connected = false;
static esp_timer_handle_t s_play_status_timer = NULL;

/********************************
 * STATIC FUNCTION DEFINITIONS
 *******************************/

static void bt_app_avrc_request_play_status(void)
{
    esp_err_t err = esp_avrc_ct_send_get_play_status_cmd(bt_avrc_common_alloc_tl());
    if (err != ESP_OK) {
        ESP_LOGW(BT_RC_CT_TAG, "Failed to request play status: %s", esp_err_to_name(err));
    }
}

// runs on BtAppTask
static void bt_app_play_status_resync_hdl(uint16_t event, void *param)
{
    (void)event;
    (void)param;
    if (s_avrc_ct_connected && player_is_playing()) {
        bt_app_avrc_request_play_status();
    }
}

// runs on the esp_timer task: only hand off, never call AVRC APIs here
static void bt_app_play_status_timer_cb(void *arg)
{
    (void)arg;
    if (s_avrc_ct_connected) {
        bt_app_work_dispatch(bt_app_play_status_resync_hdl, 0, NULL, 0, NULL, NULL);
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
        s_avrc_ct_connected = rc->conn_stat.connected;
        if (!rc->conn_stat.connected) {
            player_set_playing(false);
        }
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
            player_clear_metadata();
        } else if (rc->change_ntf.event_id == ESP_AVRC_RN_PLAY_STATUS_CHANGE) {
            //pause progress updating, resync position
            player_set_playing(rc->change_ntf.event_parameter.playback == ESP_AVRC_PLAYBACK_PLAYING);
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

        //freeze bar while seeking
        player_set_progress(rc->play_status_rsp.song_position, rc->play_status_rsp.song_length,
                            rc->play_status_rsp.play_status == ESP_AVRC_PLAYBACK_PLAYING);

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
                player_set_title((const char *)rc->meta_rsp.attr_text, (int)rc->meta_rsp.attr_length);

                ESP_LOGI(BT_RC_CT_TAG, "AVRC metadata rsp: song title: %.*s", (int)rc->meta_rsp.attr_length, rc->meta_rsp.attr_text);
            } else if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_ARTIST) {
                player_set_artist((const char *)rc->meta_rsp.attr_text, (int)rc->meta_rsp.attr_length);

                ESP_LOGI(BT_RC_CT_TAG, "AVRC metadata rsp: artist: %.*s", (int)rc->meta_rsp.attr_length, rc->meta_rsp.attr_text);
            } else if (rc->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_ALBUM) {
                player_set_album((const char *)rc->meta_rsp.attr_text, (int)rc->meta_rsp.attr_length);

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

static float bt_app_avrc_volume_to_gain(uint8_t volume)
{
    if (volume == 0) {
        return 0.0f;
    }

    float frac = (float)volume / 0x7f;
    float db = AVRC_VOLUME_MIN_DB + frac * (AVRC_VOLUME_MAX_DB - AVRC_VOLUME_MIN_DB);
    return powf(10.0f, db / 20.0f);
}

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
    switch (event) {
    /* ESP_BT_GAP_AUTH_CMPL_EVT's device_name is only populated when a *new*
     * link key is generated (bta_dm_new_link_key_cback), i.e. first-time
     * pairing -- it stays empty on every later reconnect of an already-bonded
     * phone, since no new link key is negotiated then. Actively requesting
     * the name via esp_bt_gap_read_remote_name() instead works on every
     * connection, paired or not. */
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        if (param->acl_conn_cmpl_stat.stat == ESP_BT_STATUS_SUCCESS) {
            player_set_connected(true, NULL);
            esp_bt_gap_read_remote_name(param->acl_conn_cmpl_stat.bda);
        }
        break;
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT:
        if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS) {
            char name[PLAYER_DEVICE_NAME_MAX];
            snprintf(name, sizeof(name), "%.*s", (int)(sizeof(name) - 1), (const char *)param->read_rmt_name.rmt_name);
            player_set_connected(true, name);
        }
        break;
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        player_set_connected(false, NULL);
        break;
    default:
        break;
    }

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

    const esp_timer_create_args_t play_status_timer_args = {
        .callback = bt_app_play_status_timer_cb,
        .name = "play_status_resync",
    };
    ESP_ERROR_CHECK(esp_timer_create(&play_status_timer_args, &s_play_status_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_play_status_timer, PLAY_STATUS_RESYNC_PERIOD_US));

    /* bluetooth device name, connection mode and profile set up */
    bt_app_work_dispatch(bt_av_hdl_stack_evt, BT_APP_EVT_STACK_UP, NULL, 0, NULL, NULL);
}
