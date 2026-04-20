#include "factory_assets.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_FACTORY_BADGE
#define LV_ATTRIBUTE_IMG_FACTORY_BADGE
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_IMG_FACTORY_BADGE uint8_t img_factory_badge_map[] = {
    0x00, 0x00, 0x00, 0x00, /* transparent */
    0x00, 0x00, 0x00, 0xff, /* black */

    0x07, 0xc0,
    0x1f, 0xf0,
    0x3f, 0xf8,
    0x7f, 0xfc,
    0x70, 0x0c,
    0x77, 0xec,
    0x74, 0x2c,
    0x77, 0xec,
    0x74, 0x2c,
    0x77, 0xec,
    0x70, 0x0c,
    0x7f, 0xfc,
    0x7f, 0xfc,
    0x70, 0x0c,
    0x7f, 0xfc,
};

const lv_img_dsc_t img_factory_badge = {
    .header.cf = LV_IMG_CF_INDEXED_1BIT,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 15,
    .header.h = 15,
    .data_size = sizeof(img_factory_badge_map),
    .data = img_factory_badge_map,
};
