#include "ui.h"

#include "factory_types.h"
#include "scr_mrg.h"
#include "ui_screens.h"

#include "lvgl.h"

extern "C" void factory_ui_init(void)
{
    lv_disp_t *disp = lv_disp_get_default();
    if (disp != nullptr) {
        disp->theme = lv_theme_mono_init(disp, false, LV_FONT_DEFAULT);
    }

    scr_mgr_init();
    scr_mgr_set_bg_color(0xFFFFFF);
    scr_mgr_set_anim(LV_SCR_LOAD_ANIM_NONE, LV_SCR_LOAD_ANIM_NONE, LV_SCR_LOAD_ANIM_NONE);

    scr_mgr_register(FACTORY_PAGE_HOME, factory_screen_home_lifecycle());
    scr_mgr_register(FACTORY_PAGE_DISPLAY, factory_screen_display_lifecycle());
    scr_mgr_register(FACTORY_PAGE_TOUCH, factory_screen_touch_lifecycle());
    scr_mgr_register(FACTORY_PAGE_DEVICE, factory_screen_device_lifecycle());
    scr_mgr_register(FACTORY_PAGE_BATTERY, factory_placeholder_lifecycle(FACTORY_PAGE_BATTERY));
    scr_mgr_register(FACTORY_PAGE_WIFI, factory_placeholder_lifecycle(FACTORY_PAGE_WIFI));
    scr_mgr_register(FACTORY_PAGE_SD, factory_placeholder_lifecycle(FACTORY_PAGE_SD));
    scr_mgr_register(FACTORY_PAGE_GPS, factory_placeholder_lifecycle(FACTORY_PAGE_GPS));
    scr_mgr_register(FACTORY_PAGE_LORA, factory_placeholder_lifecycle(FACTORY_PAGE_LORA));

#ifdef CONFIG_FACTORY_BOOT_TOUCH_DIAGNOSTICS
    scr_mgr_switch(FACTORY_PAGE_TOUCH, false);
#else
    scr_mgr_switch(FACTORY_PAGE_HOME, false);
#endif
}
