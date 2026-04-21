#include "ui_theme.h"

#include "factory_assets.h"
#include "scr_mrg.h"

namespace {

constexpr lv_coord_t kBackButtonTop = 18;
constexpr lv_coord_t kBackButtonHeight = 46;
constexpr lv_coord_t kBackButtonBottom = kBackButtonTop + kBackButtonHeight;
constexpr lv_coord_t kPanelTop = kBackButtonBottom + 30;

static bool text_contains_lv_symbol(const char *text)
{
    if (text == nullptr) {
        return false;
    }

    const unsigned char *p = (const unsigned char *)text;
    while (*p != '\0') {
        if ((*p & 0xF0U) == 0xE0U && p[1] != '\0' && p[2] != '\0') {
            const uint32_t codepoint = ((uint32_t)(p[0] & 0x0FU) << 12) |
                                       ((uint32_t)(p[1] & 0x3FU) << 6) |
                                       (uint32_t)(p[2] & 0x3FU);
            if (codepoint >= 0xF000U && codepoint <= 0xF8FFU) {
                return true;
            }
            p += 3;
            continue;
        }

        if ((*p & 0xE0U) == 0xC0U && p[1] != '\0') {
            p += 2;
            continue;
        }

        if ((*p & 0xF8U) == 0xF0U && p[1] != '\0' && p[2] != '\0' && p[3] != '\0') {
            p += 4;
            continue;
        }

        ++p;
    }

    return false;
}

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
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 36, 26);
    return label;
}

extern "C" lv_obj_t *factory_ui_create_subtitle(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(88));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, FACTORY_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
    return label;
}

extern "C" lv_obj_t *factory_ui_create_content_panel(lv_obj_t *parent, lv_coord_t width_pct, lv_coord_t height_pct)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, lv_pct(width_pct), lv_pct(height_pct));
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, kPanelTop);
    lv_obj_set_style_bg_color(panel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(panel, 0, LV_PART_MAIN);
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
    lv_obj_set_style_text_font(
        label,
        text_contains_lv_symbol(text) ? FACTORY_FONT_SYMBOL : FACTORY_FONT_BODY,
        LV_PART_MAIN);
    return btn;
}

extern "C" lv_obj_t *factory_ui_create_menu_tile(lv_obj_t *parent, const char *symbol, const char *title, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (cb != nullptr) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, FACTORY_FONT_SYMBOL, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_black(), LV_PART_MAIN);

    lv_obj_t *text = lv_label_create(btn);
    lv_label_set_text(text, title);
    lv_obj_set_style_text_font(text, FACTORY_FONT_UI_HOME_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_color(text, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    return btn;
}

extern "C" void factory_ui_create_back_button(lv_obj_t *parent, const char *title)
{
    lv_obj_t *btn = factory_ui_create_action_button(parent, LV_SYMBOL_LEFT, back_btn_event_cb, nullptr);
    lv_obj_set_size(btn, 60, 46);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 18, kBackButtonTop);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_align_to(label, btn, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    lv_obj_set_style_text_font(label, FACTORY_FONT_UI_HOME_TEXT, LV_PART_MAIN);
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
    lv_obj_set_style_text_font(title_label, FACTORY_FONT_BODY, LV_PART_MAIN);

    lv_obj_t *value_label = lv_label_create(card);
    lv_obj_set_width(value_label, lv_pct(100));
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(value_label, value);
    lv_obj_align_to(value_label, title_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_set_style_text_font(value_label, FACTORY_FONT_BODY, LV_PART_MAIN);
    return card;
}
