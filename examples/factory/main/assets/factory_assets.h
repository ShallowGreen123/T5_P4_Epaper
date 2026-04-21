#pragma once

#include "lvgl.h"

#define FACTORY_FONT_TITLE &Font_Mono_Bold_25
#define FACTORY_FONT_BODY &Font_Mono_Bold_20
#if LV_FONT_MONTSERRAT_28
#define FACTORY_FONT_SYMBOL &lv_font_montserrat_28
#else
#define FACTORY_FONT_SYMBOL LV_FONT_DEFAULT
#endif

#define FACTORY_FONT_UI_HOME_TEXT &Font_Mono_Bold_30

#define FACTORY_FONT_UI_WIFI_STATE &Font_Mono_Bold_25
#define FACTORY_FONT_UI_WIFI_SUMMARY &Font_Mono_Bold_20

// 字体声明
LV_FONT_DECLARE(Font_Geist_Bold_20);
LV_FONT_DECLARE(Font_Geist_Light_20);
LV_FONT_DECLARE(Font_Mono_Bold_20);
LV_FONT_DECLARE(Font_Mono_Bold_25);
LV_FONT_DECLARE(Font_Mono_Bold_30);
LV_FONT_DECLARE(Font_Mono_Bold_90);

// 图片声明
