#pragma once
#include <stdint.h>

#define INTERVAL_READ_REG 200 /* unit:ms */

#define FTS_CMD_READ_ID 0x90

/* chip id */
#define FTS_CHIP_IDH 0x52
#define FTS_CHIP_IDL 0x60

/* register address */
#define FTS_REG_CHIP_ID 0xA3
#define FTS_REG_CHIP_ID2 0x9F
#define FTS_REG_FW_VER 0xA6
#define FTS_REG_UPGRADE 0xFC

int ft5536_read(uint8_t addr, uint8_t *data, uint16_t datalen);
int ft5536_read_reg(uint8_t addr, uint8_t *value);
int ft5536_write(uint8_t addr, uint8_t *data, uint16_t datalen);
int ft5536_write_reg(uint8_t addr, uint8_t value);

int ft5536_check_id(void);
int fts_touch_process(void);
bool fts_touch_is_pressed(void);
int fts_touch_get_xy(uint16_t * x, uint16_t * y);
