#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void factory_ui_apply_screen(lv_obj_t *screen);
lv_obj_t *factory_ui_create_title(lv_obj_t *parent, const char *title);
lv_obj_t *factory_ui_create_subtitle(lv_obj_t *parent, const char *text);
lv_obj_t *factory_ui_create_content_panel(lv_obj_t *parent, lv_coord_t width_pct, lv_coord_t height_pct);
lv_obj_t *factory_ui_create_action_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data);
void factory_ui_create_back_button(lv_obj_t *parent, const char *title);
lv_obj_t *factory_ui_create_info_label(lv_obj_t *parent, const char *text);
lv_obj_t *factory_ui_create_value_card(lv_obj_t *parent, const char *title, const char *value);

#ifdef __cplusplus
}  // extern "C"
#endif
