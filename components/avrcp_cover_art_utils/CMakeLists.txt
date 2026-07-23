idf_component_register(SRCS "avrcp_cover_art_utils.c"
                            "avrcp_cover_art_service.c"
                    PRIV_REQUIRES bt esp_lcd esp_lcd_touch esp_lvgl_port lvgl avrcp_common_utils
                    INCLUDE_DIRS "." "${PROJECT_DIR}/main" "${IDF_PATH}/examples/bluetooth/bluedroid/classic_bt/common/avrcp_utils/include")
