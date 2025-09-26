/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "Arduino.h"
#include <FastEPD.h>
#include "smiley.h"

FASTEPD epaper;
void setup(void)
{
      int i, j;
  float f;

// This configuration for this PCB contians info about the Eink connections and display type
  epaper.initPanel(BB_PANEL_LILYGO_T5P4);
//   epaper.clearWhite(); // start with a white display (and buffer)
//   // The smiley image is 100x100 pixels; draw it at various scales from 0.5 to 2.0
//   i = 0;
//   f = 0.5f; // start at 1/2 size (50x50)
//   for (j = 0; j < 5; j++) {
//     epaper.loadG5Image(smiley, i, i, BBEP_WHITE, BBEP_BLACK, f);
//     i += (int)(100.0f * f);
//     f += 0.5f;
//   }
//   epaper.partialUpdate(false); // the flag (false) tells it to turn off eink power after the update
//   epaper.deInit(); // save power by shutting down the TI power controller and I/O extender
}


void loop()
{
    printf("hello\n");
    delay(1000);
}