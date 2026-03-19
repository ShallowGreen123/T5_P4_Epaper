#pragma once

#include <stdint.h>

// NOTE: Arduino Wire uses 7-bit I2C addresses.
// LT8912 boards/variants are commonly seen at 0x2C or 0x48..0x4B.
// This board responds on 0x48, but the probe code still checks the common alternates.
#define HDMI_LT8912_I2C_ADDR (0x48)

#define BOARD_I2C_SDA (7)
#define BOARD_I2C_SCL (8)

#define BOARD_SPI_MISO (44)
#define BOARD_SPI_SCK (45)
#define BOARD_SPI_MOSI (46)

#define BOARD_SD_CS (47)
#define BOARD_SD_MISO BOARD_SPI_MISO
#define BOARD_SD_SCK BOARD_SPI_SCK
#define BOARD_SD_MOSI BOARD_SPI_MOSI

#define BOARD_PCA_06_HDMI_RST (6)
#define BOARD_PCA_07_HDMI_EN (7)
// Powers the 1V8 rail used by peripherals (required by LT8912 on some boards).
#define BOARD_PCA_12_1V8_EN (10)

#define BOARD_HDMI_RST (BOARD_PCA_06_HDMI_RST)
#define BOARD_HDMI_INT (4)
#define BOARD_HDMI_SDA (BOARD_I2C_SDA)
#define BOARD_HDMI_SCL (BOARD_I2C_SCL)
#define BOARD_HDMI_DDC_SDA (9)
#define BOARD_HDMI_DDC_SCL (10)

// ESP32-P4 MIPI DSI PHY power rail configuration.
#define BOARD_MIPI_DSI_PHY_PWR_LDO_CHAN (3)
#define BOARD_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV (2500)

// Reduced-bandwidth HDMI mode for stable MJPEG playback on LT8912B.
#define HDMI_FRAME_WIDTH (800)
#define HDMI_FRAME_HEIGHT (600)
#define HDMI_BYTES_PER_PIXEL (3)
#define HDMI_DPI_CLOCK_MHZ (40.0f)   // 800x600@60Hz timing
#define HDMI_HSYNC_BACK_PORCH (88)
#define HDMI_HSYNC_PULSE_WIDTH (128)
#define HDMI_HSYNC_FRONT_PORCH (48)
#define HDMI_VSYNC_BACK_PORCH (23)
#define HDMI_VSYNC_PULSE_WIDTH (4)
#define HDMI_VSYNC_FRONT_PORCH (1)
#define HDMI_DSI_LANE_BIT_RATE_MBPS (1000)
#define HDMI_FRAMEBUFFER_COUNT (3)


