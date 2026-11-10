set(EXTRA_COMPONENTS a2dp_sink_int_codec_utils)

idf_component_register(SRCS "main.c" "audio/bluetooth/sink.c" "ui/lvgl.c" "state/player.c" "ui/screens/loader.c" "ui/screens/main.c" "ui/ui_helpers.c" "ui/fonts/ui_font_Icons.c" "ui/fonts/inter_12.c" "ui/fonts/inter_20.c" "ui/animations.c"
                    PRIV_REQUIRES bt bt_app_core_utils bredr_app_common_utils a2dp_sink_common_utils esp_lcd esp_lcd_touch avrcp_common_utils esp_common esp_driver_ledc
                    PRIV_REQUIRES ${EXTRA_COMPONENTS} avrcp_common_utils avrcp_metadata_utils avrcp_cover_art_utils
                    INCLUDE_DIRS ".")
