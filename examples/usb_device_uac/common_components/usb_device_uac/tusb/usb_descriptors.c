/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 * Copyright (c) 2020 Jerzy Kasenbreg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <string.h>

#include "tusb.h"
#include "uac_descriptors.h"

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    // Use Interface Association Descriptor (IAD) for CDC
    // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = CONFIG_UAC_TUSB_VID,
    .idProduct          = CONFIG_UAC_TUSB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

#if TUD_OPT_HIGH_SPEED
static tusb_desc_device_qualifier_t const desc_device_qualifier = {
    .bLength            = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved          = 0x00,
};

uint8_t const *tud_descriptor_device_qualifier_cb(void)
{
    return (uint8_t const *)&desc_device_qualifier;
}
#endif

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
#define CONFIG_TOTAL_LEN        (TUD_CONFIG_DESC_LEN + CFG_TUD_AUDIO * TUD_AUDIO_DEVICE_DESC_LEN)
#define EPNUM_AUDIO_OUT   0x01
#define EPNUM_AUDIO_FB    0x81
#define EPNUM_AUDIO_IN    0x82
#define AUDIO_FS_SPK_INTERVAL 1
#define AUDIO_FS_MIC_INTERVAL 1
#define AUDIO_HS_SPK_INTERVAL 1
#define AUDIO_HS_MIC_INTERVAL 4
#define AUDIO_FS_FEEDBACK_SIZE 4
#define AUDIO_HS_FEEDBACK_SIZE 4

#if TUD_OPT_HIGH_SPEED
#define AUDIO_PRIMARY_SPK_INTERVAL AUDIO_HS_SPK_INTERVAL
#define AUDIO_PRIMARY_MIC_INTERVAL AUDIO_HS_MIC_INTERVAL
#define AUDIO_PRIMARY_FEEDBACK_SIZE AUDIO_HS_FEEDBACK_SIZE
#define AUDIO_PRIMARY_EP_OUT_SIZE CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_OUT_HS
#define AUDIO_PRIMARY_EP_IN_SIZE CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_IN_HS
#else
#define AUDIO_PRIMARY_SPK_INTERVAL AUDIO_FS_SPK_INTERVAL
#define AUDIO_PRIMARY_MIC_INTERVAL AUDIO_FS_MIC_INTERVAL
#define AUDIO_PRIMARY_FEEDBACK_SIZE AUDIO_FS_FEEDBACK_SIZE
#define AUDIO_PRIMARY_EP_OUT_SIZE CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_OUT_FS
#define AUDIO_PRIMARY_EP_IN_SIZE CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_IN_FS
#endif

uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    // Interface number, string index, EP Out & EP In address, EP size
    TUD_AUDIO_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, EPNUM_AUDIO_OUT, EPNUM_AUDIO_IN, EPNUM_AUDIO_FB,
                         AUDIO_PRIMARY_SPK_INTERVAL, AUDIO_PRIMARY_MIC_INTERVAL,
                         AUDIO_PRIMARY_FEEDBACK_SIZE, AUDIO_PRIMARY_EP_OUT_SIZE, AUDIO_PRIMARY_EP_IN_SIZE),
};

TU_VERIFY_STATIC(sizeof(desc_configuration) == CONFIG_TOTAL_LEN, "UAC configuration descriptor length mismatch");

#if TUD_OPT_HIGH_SPEED
static uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_AUDIO_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, EPNUM_AUDIO_OUT, EPNUM_AUDIO_IN, EPNUM_AUDIO_FB,
                         AUDIO_FS_SPK_INTERVAL, AUDIO_FS_MIC_INTERVAL, AUDIO_FS_FEEDBACK_SIZE,
                         CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_OUT_FS,
                         CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_IN_FS),
};

static uint8_t desc_other_speed_configuration[CONFIG_TOTAL_LEN];

TU_VERIFY_STATIC(sizeof(desc_fs_configuration) == CONFIG_TOTAL_LEN,
                 "UAC full-speed configuration descriptor length mismatch");
#endif

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index; // for multiple configurations
#if TUD_OPT_HIGH_SPEED
    if (tud_speed_get() == TUSB_SPEED_FULL) {
        return desc_fs_configuration;
    }
#endif
    return desc_configuration;
}

#if TUD_OPT_HIGH_SPEED
uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index)
{
    (void)index;
    uint8_t const *other_speed = tud_speed_get() == TUSB_SPEED_HIGH
                                 ? desc_fs_configuration
                                 : desc_configuration;
    memcpy(desc_other_speed_configuration, other_speed, sizeof(desc_other_speed_configuration));
    desc_other_speed_configuration[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
    return desc_other_speed_configuration;
}
#endif

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// array of pointer to string descriptors
char const *string_desc_arr [] = {
    (const char[]) { 0x09, 0x04 },  // 0: is supported language is English (0x0409)
    CONFIG_UAC_TUSB_MANUFACTURER,       // 1: Manufacturer
    CONFIG_UAC_TUSB_PRODUCT,            // 2: Product
    CONFIG_UAC_TUSB_SERIAL_NUM,         // 3: Serials, should use chip ID
    "usb uac",                      // 4: UAC control Interface
#if SPEAK_CHANNEL_NUM
    "speaker",                     // 5: Speak Interface
#endif
#if MIC_CHANNEL_NUM
    "microphone",                   // 6: Mic Interface
#endif
};

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {

        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
            return NULL;
        }

        const char *str = string_desc_arr[index];

        // Cap at max char
        chr_count = (uint8_t) strlen(str);
        if (chr_count > 31) {
            chr_count = 31;
        }

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return _desc_str;
}
