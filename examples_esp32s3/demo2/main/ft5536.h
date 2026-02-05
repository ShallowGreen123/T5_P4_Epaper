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

/*Max point numbers of touch trace*/
#define MAX_POINTS_TOUCH_TRACE 2
/*Length of touch information*/
#define MAX_LEN_TOUCH_INFO (MAX_POINTS_TOUCH_TRACE * 6 + 2)
/*Max touch points that touch controller supports*/
#define FTS_MAX_POINTS_SUPPORT 10


struct fts_ts_event
{
    int x;    /*x coordinate */
    int y;    /*y coordinate */
    int p;    /* pressure */
    int type; /* touch event flag: 0 -- down; 1-- up; 2 -- contact */
    int id;   /*touch ID */
    int area; /*touch area*/
};

int ft5536_read(uint8_t addr, uint8_t *data, uint16_t datalen);
int ft5536_read_reg(uint8_t addr, uint8_t *value);
int ft5536_write(uint8_t addr, uint8_t *data, uint16_t datalen);
int ft5536_write_reg(uint8_t addr, uint8_t value);

int ft5536_check_id(void);
int fts_touch_process(void);
