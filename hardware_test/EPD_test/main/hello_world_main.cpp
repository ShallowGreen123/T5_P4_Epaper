/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "Arduino.h"
#include <FastEPD.h>
#include "smiley.h"

FASTEPD epaper;

#define REFESH_LOOP 1

void setup(void)
{
    int i, j;
    float f;

    i = 0;
    f = 0.5f; // start at 1/2 size (50x50)

    // This configuration for this PCB contians info about the Eink connections and display type
    // epaper.initPanel(BB_PANEL_V7_RAW, 26666666);
    // epaper.setPanelSize(BBEP_DISPLAY_EC060TC1);
    epaper.initPanel(BB_PANEL_LILYGO_T5P4, 26666666);
    epaper.fillScreen(BBEP_WHITE);
    delay(1000);
#if (REFESH_LOOP!=1 )
    epaper.clearWhite(); // start with a white display (and buffer)
    // The smiley image is 100x100 pixels; draw it at various scales from 0.5 to 2.0
    i = 0;
    f = 0.5f; // start at 1/2 size (50x50)
    for (j = 0; j < 12; j++)
    {
        epaper.loadG5Image(smiley, i, i, BBEP_WHITE, BBEP_BLACK, f);
        i += (int)(100.0f * f);
        f += 0.5f;
    }
    epaper.setPasses(7);
    epaper.partialUpdate(false); // the flag (false) tells it to turn off eink power after the update
    epaper.deInit();             // save power by shutting down the TI power controller and I/O extender
#endif
}

int r = 0;

void loop()
{
#if REFESH_LOOP 
    int i, j;
    float f;

    i = 0;
    f = 0.5f; // start at 1/2 size (50x50)

    epaper.clearWhite(); // start with a white display (and buffer)

    // The smiley image is 100x100 pixels; draw it at various scales from 0.5 to 2.0
    i = 0;
    f = 0.5f; // start at 1/2 size (50x50)
    for (j = 0; j < 12; j++)
    {
        epaper.loadG5Image(smiley, i, i, BBEP_WHITE, BBEP_BLACK, f);
        i += (int)(100.0f * f);
        f += 0.5f;
    }
    epaper.setPasses(7);

    epaper.setRotation(r * 90);
    r++;
    r &= 0x3;

    // epaper.setRotation(270);
    epaper.partialUpdate(false); // the flag (false) tells it to turn off eink power after the update
    delay(2000);
#else 
    printf("hello\n");
    delay(1000);
#endif
    
}