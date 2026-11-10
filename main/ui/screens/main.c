#include "../ui.h"
#include "../animations.h"

lv_obj_t *ui_Screen1 = NULL;
lv_obj_t *ui_TabView1 = NULL;
lv_obj_t *ui_Home = NULL;
lv_obj_t *ui_Label2 = NULL;
lv_obj_t *ui_Label3 = NULL;
lv_obj_t *ui_Search = NULL;
lv_obj_t *ui_TextArea1 = NULL;
lv_obj_t *ui_Now_Playing = NULL;
lv_obj_t *ui_album_image = NULL;
lv_obj_t *ui_title_label = NULL;
lv_obj_t *ui_Back = NULL;
lv_obj_t *ui_Label5 = NULL;
lv_obj_t *ui_Back1 = NULL;
lv_obj_t *ui_Label1 = NULL;
lv_obj_t *ui_Back2 = NULL;
lv_obj_t *ui_Label6 = NULL;
lv_obj_t *ui_progress_bar = NULL;
lv_obj_t *ui_subtitle_label = NULL;
lv_obj_t *ui_connected_label = NULL;
lv_obj_t *ui_Settings = NULL;
lv_obj_t *ui_Label9 = NULL;
lv_obj_t *ui_Slider1 = NULL;
lv_obj_t *ui_Keyboard = NULL;
// event funtions
void ui_event_TextArea1(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_FOCUSED)
    {
        _ui_flag_modify(ui_Keyboard, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_REMOVE);
    }
    if (event_code == LV_EVENT_DEFOCUSED)
    {
        _ui_flag_modify(ui_Keyboard, LV_OBJ_FLAG_HIDDEN, _UI_MODIFY_FLAG_ADD);
    }
}

void ui_event_brightness(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);

    int value = lv_slider_get_value(slider);

    lvgl_set_backlight_brightness(value);
}

// build funtions

void ui_main_screen_init(void)
{
    ui_Screen1 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen1, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    lv_scr_load(ui_Screen1);

    ui_TabView1 = lv_tabview_create(ui_Screen1);
    lv_tabview_set_tab_bar_position(ui_TabView1, LV_DIR_LEFT);
    lv_tabview_set_tab_bar_size(ui_TabView1, 50);
    lv_obj_set_width(ui_TabView1, 320);
    lv_obj_set_height(ui_TabView1, 240);
    lv_obj_clear_flag(ui_TabView1, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_set_style_bg_color(ui_TabView1, lv_color_hex(0x101010), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_TabView1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_text_color(lv_tabview_get_tab_btns(ui_TabView1), lv_color_hex(0xB4B4B4),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lv_tabview_get_tab_btns(ui_TabView1), 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lv_tabview_get_tab_btns(ui_TabView1), &ui_font_Icons, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(lv_tabview_get_tab_btns(ui_TabView1), lv_color_hex(0x000000),
                              LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(lv_tabview_get_tab_btns(ui_TabView1), 255, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(lv_tabview_get_tab_btns(ui_TabView1), LV_BORDER_SIDE_RIGHT,
                                 LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lv_tabview_get_tab_btns(ui_TabView1), lv_color_hex(0xFFFFFF),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_opa(lv_tabview_get_tab_btns(ui_TabView1), 255, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(lv_tabview_get_tab_btns(ui_TabView1), lv_color_hex(0x000000),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(lv_tabview_get_tab_btns(ui_TabView1), 255, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(lv_tabview_get_tab_btns(ui_TabView1), lv_color_hex(0xFFFFFF),
                                  LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(lv_tabview_get_tab_btns(ui_TabView1), 255, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(lv_tabview_get_tab_btns(ui_TabView1), 2, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(lv_tabview_get_tab_btns(ui_TabView1), LV_BORDER_SIDE_LEFT,
                                 LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_clear_flag(lv_tabview_get_content(ui_TabView1), LV_OBJ_FLAG_SCROLLABLE);

    ui_Now_Playing = lv_tabview_add_tab(ui_TabView1, "");
    ui_Home = lv_tabview_add_tab(ui_TabView1, "");

    ui_Label2 = lv_label_create(ui_Home);
    lv_obj_set_width(ui_Label2, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_Label2, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_Label2, 0);
    lv_obj_set_y(ui_Label2, -20);
    lv_obj_set_align(ui_Label2, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label2, "Coupe");
    lv_obj_set_style_text_color(ui_Label2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Label2, &inter_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label3 = lv_label_create(ui_Home);
    lv_obj_set_width(ui_Label3, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_Label3, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_Label3, 0);
    lv_obj_set_y(ui_Label3, 10);
    lv_obj_set_align(ui_Label3, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label3, "Version 1");
    lv_obj_set_style_text_color(ui_Label3, lv_color_hex(0xAFAFAF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Label3, &inter_12, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Search = lv_tabview_add_tab(ui_TabView1, "");

    ui_TextArea1 = lv_textarea_create(ui_Search);
    lv_obj_set_width(ui_TextArea1, 232);
    lv_obj_set_height(ui_TextArea1, LV_SIZE_CONTENT); /// 36
    lv_obj_set_align(ui_TextArea1, LV_ALIGN_TOP_MID);
    lv_textarea_set_placeholder_text(ui_TextArea1, "Search for a song...");
    lv_textarea_set_one_line(ui_TextArea1, true);
    lv_obj_set_style_text_color(ui_TextArea1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_TextArea1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_TextArea1, &inter_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_TextArea1, lv_color_hex(0x232323), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_TextArea1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_TextArea1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_TextArea1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_text_color(ui_TextArea1, lv_color_hex(0xFFFFFF), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_TextArea1, 255, LV_PART_SELECTED | LV_STATE_DEFAULT);

    lv_obj_clear_flag(ui_Now_Playing, LV_OBJ_FLAG_SCROLLABLE); /// Flags

    ui_album_image = lv_img_create(ui_Now_Playing);
    lv_obj_set_width(ui_album_image, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_album_image, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_album_image, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_album_image, LV_OBJ_FLAG_ADV_HITTEST); /// Flags
    lv_obj_clear_flag(ui_album_image, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                          LV_OBJ_FLAG_SCROLL_CHAIN); /// Flags
    lv_obj_set_style_opa(ui_album_image, 75, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_img_set_zoom(ui_album_image, 345);

    ui_title_label = lv_label_create(ui_Now_Playing);
    lv_obj_set_width(ui_title_label, 230);
    lv_obj_set_height(ui_title_label, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_title_label, 7);
    lv_obj_set_y(ui_title_label, -65);
    lv_obj_set_align(ui_title_label, LV_ALIGN_BOTTOM_LEFT);
    lv_label_set_long_mode(ui_title_label, LV_LABEL_LONG_SCROLL);
    lv_label_set_text(ui_title_label, "Not Playing");
    lv_obj_clear_flag(ui_title_label, LV_OBJ_FLAG_SCROLLABLE); /// Flags
    lv_obj_set_style_text_color(ui_title_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_title_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_title_label, &inter_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Back = lv_btn_create(ui_Now_Playing);
    lv_obj_set_width(ui_Back, 40);
    lv_obj_set_height(ui_Back, 40);
    lv_obj_set_x(ui_Back, 0);
    lv_obj_set_y(ui_Back, 3);
    lv_obj_set_align(ui_Back, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_add_flag(ui_Back, LV_OBJ_FLAG_SCROLL_ON_FOCUS); /// Flags
    lv_obj_clear_flag(ui_Back, LV_OBJ_FLAG_SCROLLABLE);    /// Flags
    lv_obj_set_style_bg_color(ui_Back, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui_Back, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_Back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_Back, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Back, &ui_font_Icons, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label5 = lv_label_create(ui_Back);
    lv_obj_set_width(ui_Label5, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_Label5, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_Label5, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label5, "");
    lv_obj_set_style_text_color(ui_Label5, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Label5, &ui_font_Icons, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Back1 = lv_btn_create(ui_Now_Playing);
    lv_obj_set_width(ui_Back1, 40);
    lv_obj_set_height(ui_Back1, 40);
    lv_obj_set_x(ui_Back1, 0);
    lv_obj_set_y(ui_Back1, 3);
    lv_obj_set_align(ui_Back1, LV_ALIGN_BOTTOM_MID);
    lv_obj_add_flag(ui_Back1, LV_OBJ_FLAG_SCROLL_ON_FOCUS); /// Flags
    lv_obj_clear_flag(ui_Back1, LV_OBJ_FLAG_SCROLLABLE);    /// Flags
    lv_obj_set_style_bg_color(ui_Back1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Back1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui_Back1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_Back1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_Back1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Back1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Back1, &ui_font_Icons, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label1 = lv_label_create(ui_Back1);
    lv_obj_set_width(ui_Label1, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_Label1, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_Label1, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label1, "");
    lv_obj_set_style_text_color(ui_Label1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Label1, &ui_font_Icons, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Back2 = lv_btn_create(ui_Now_Playing);
    lv_obj_set_width(ui_Back2, 40);
    lv_obj_set_height(ui_Back2, 40);
    lv_obj_set_x(ui_Back2, 0);
    lv_obj_set_y(ui_Back2, 3);
    lv_obj_set_align(ui_Back2, LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_add_flag(ui_Back2, LV_OBJ_FLAG_SCROLL_ON_FOCUS); /// Flags
    lv_obj_clear_flag(ui_Back2, LV_OBJ_FLAG_SCROLLABLE);    /// Flags
    lv_obj_set_style_bg_color(ui_Back2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Back2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui_Back2, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_Back2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_Back2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Back2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Back2, &ui_font_Icons, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Label6 = lv_label_create(ui_Back2);
    lv_obj_set_width(ui_Label6, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_Label6, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_Label6, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label6, "");
    lv_obj_set_style_text_color(ui_Label6, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Label6, &ui_font_Icons, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_progress_bar = lv_bar_create(ui_Now_Playing);
    lv_bar_set_value(ui_progress_bar, 0, LV_ANIM_OFF);
    lv_bar_set_start_value(ui_progress_bar, 0, LV_ANIM_OFF);
    lv_bar_set_min_value(ui_progress_bar, 0);
    lv_bar_set_max_value(ui_progress_bar, 1);
    lv_obj_set_width(ui_progress_bar, 231);
    lv_obj_set_height(ui_progress_bar, 3);
    lv_obj_set_x(ui_progress_bar, 0);
    lv_obj_set_y(ui_progress_bar, -39);
    lv_obj_set_align(ui_progress_bar, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_bg_color(ui_progress_bar, lv_color_hex(0x434343), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_progress_bar, 100, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(ui_progress_bar, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_progress_bar, 150, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    ui_subtitle_label = lv_label_create(ui_Now_Playing);
    lv_obj_set_width(ui_subtitle_label, 229);
    lv_obj_set_height(ui_subtitle_label, LV_SIZE_CONTENT); /// 1
    lv_obj_set_x(ui_subtitle_label, 6);
    lv_obj_set_y(ui_subtitle_label, -49);
    lv_obj_set_align(ui_subtitle_label, LV_ALIGN_BOTTOM_LEFT);
    lv_label_set_long_mode(ui_subtitle_label, LV_LABEL_LONG_SCROLL);
    lv_label_set_text(ui_subtitle_label, "Now discoverable as Coupe.");
    lv_obj_set_style_text_color(ui_subtitle_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_subtitle_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_subtitle_label, &inter_12, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_style(ui_title_label, &anim_text_scroll_sync, LV_PART_MAIN);
    lv_obj_add_style(ui_subtitle_label, &anim_text_scroll_sync, LV_PART_MAIN);

    ui_connected_label = lv_label_create(ui_Now_Playing);
    lv_obj_set_width(ui_connected_label, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_connected_label, LV_SIZE_CONTENT); /// 1
    lv_obj_set_align(ui_connected_label, LV_ALIGN_TOP_RIGHT);
    lv_label_set_text(ui_connected_label, "");
    lv_obj_set_style_text_color(ui_connected_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_connected_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_connected_label, &ui_font_Icons, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Settings = lv_tabview_add_tab(ui_TabView1, "");

    ui_Label9 = lv_label_create(ui_Settings);
    lv_obj_set_width(ui_Label9, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height(ui_Label9, LV_SIZE_CONTENT); /// 1
    lv_label_set_text(ui_Label9, "Brightness");
    lv_obj_set_style_text_color(ui_Label9, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Label9, &inter_12, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Slider1 = lv_slider_create(ui_Settings);
    lv_slider_set_value(ui_Slider1, 50, LV_ANIM_OFF);
    if (lv_slider_get_mode(ui_Slider1) == LV_SLIDER_MODE_RANGE)
        lv_slider_set_left_value(ui_Slider1, 0, LV_ANIM_OFF);
    lv_obj_set_width(ui_Slider1, 242);
    lv_obj_set_height(ui_Slider1, 10);
    lv_obj_set_x(ui_Slider1, 0);
    lv_obj_set_y(ui_Slider1, 24);
    lv_obj_set_style_bg_color(ui_Slider1, lv_color_hex(0x353535), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Slider1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(ui_Slider1, lv_color_hex(0xD9D9D9), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Slider1, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(ui_Slider1, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Slider1, 255, LV_PART_KNOB | LV_STATE_DEFAULT);

    /* LVGL 9's tabview tab bar holds individual lv_button children (not a
     * button-matrix like LVGL 8), so the per-item bg/border styling has to be
     * applied to each tab button directly via LV_PART_MAIN -- LV_PART_ITEMS
     * has no effect on a plain container and was silently ignored. */
    for (uint32_t tab_btn_i = 0; tab_btn_i < lv_tabview_get_tab_count(ui_TabView1); tab_btn_i++)
    {
        lv_obj_t *tab_btn = lv_tabview_get_tab_button(ui_TabView1, tab_btn_i);
        lv_obj_set_style_bg_color(tab_btn, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(tab_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_side(tab_btn, LV_BORDER_SIDE_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_set_style_text_color(tab_btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_opa(tab_btn, 255, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(tab_btn, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(tab_btn, 255, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(tab_btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_border_opa(tab_btn, 255, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_border_width(tab_btn, 2, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_border_side(tab_btn, LV_BORDER_SIDE_LEFT, LV_PART_MAIN | LV_STATE_CHECKED);
    }

    ui_Keyboard = lv_keyboard_create(ui_Screen1);
    lv_obj_set_width(ui_Keyboard, 320);
    lv_obj_set_height(ui_Keyboard, 120);
    lv_obj_set_align(ui_Keyboard, LV_ALIGN_BOTTOM_MID);
    lv_obj_add_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN); /// Flags
    lv_obj_set_style_text_font(ui_Keyboard, &inter_12, LV_PART_ITEMS | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_TextArea1, ui_event_TextArea1, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_Slider1, ui_event_brightness, LV_EVENT_ALL, NULL);
    lv_keyboard_set_textarea(ui_Keyboard, ui_TextArea1);
}

void ui_main_screen_destroy(void)
{
    if (ui_Screen1)
        lv_obj_del(ui_Screen1);

    // NULL screen variables
    ui_Screen1 = NULL;
    ui_TabView1 = NULL;
    ui_Home = NULL;
    ui_Label2 = NULL;
    ui_Label3 = NULL;
    ui_Search = NULL;
    ui_TextArea1 = NULL;
    ui_Now_Playing = NULL;
    ui_album_image = NULL;
    ui_title_label = NULL;
    ui_Back = NULL;
    ui_Label5 = NULL;
    ui_Back1 = NULL;
    ui_Label1 = NULL;
    ui_Back2 = NULL;
    ui_Label6 = NULL;
    ui_progress_bar = NULL;
    ui_subtitle_label = NULL;
    ui_connected_label = NULL;
    ui_Settings = NULL;
    ui_Label9 = NULL;
    ui_Slider1 = NULL;
    ui_Keyboard = NULL;
}
