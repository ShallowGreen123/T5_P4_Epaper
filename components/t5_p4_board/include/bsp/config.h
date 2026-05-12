/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/* This board layer ships as a lightweight non-LVGL BSP subset by default. */
#if !defined(BSP_CONFIG_NO_GRAPHIC_LIB)
#define BSP_CONFIG_NO_GRAPHIC_LIB (1)
#endif
