#include "ui_theme.h"

#include "asset/factory_assets.h"
#include "scr_mrg.h"

namespace {

static void back_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        scr_mgr_pop(false);
    }
}

}  // namespace

extern "C" void factory_ui_apply_screen(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
}

extern "C" lv_obj_t *factory_ui_create_title(lv_obj_t *parent, const char *title)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, FACTORY_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 28, 24);
    return label;
}

extern "C" lv_obj_t *factory_ui_create_subtitle(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(90));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    return label;
}

extern "C" lv_obj_t *factory_ui_create_content_panel(lv_obj_t *parent, lv_coord_t width_pct, lv_coord_t height_pct)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, lv_pct(width_pct), lv_pct(height_pct));
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_bg_color(panel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 12, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    return panel;
}

extern "C" lv_obj_t *factory_ui_create_action_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_height(btn, 58);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 14, LV_PART_MAIN);
    if (cb != nullptr) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, FACTORY_FONT_BODY, LV_PART_MAIN);
    return btn;
}

extern "C" void factory_ui_create_back_button(lv_obj_t *parent, const char *title)
{
    lv_obj_t *btn = factory_ui_create_action_button(parent, LV_SYMBOL_LEFT, back_btn_event_cb, nullptr);
    lv_obj_set_size(btn, 60, 46);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 18, 18);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_align_to(label, btn, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    lv_obj_set_style_text_font(label, FACTORY_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
}

extern "C" lv_obj_t *factory_ui_create_info_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    return label;
}

extern "C" lv_obj_t *factory_ui_create_value_card(lv_obj_t *parent, const char *title, const char *value)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *value_label = lv_label_create(card);
    lv_obj_set_width(value_label, lv_pct(100));
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(value_label, value);
    lv_obj_align_to(value_label, title_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    return card;
}
