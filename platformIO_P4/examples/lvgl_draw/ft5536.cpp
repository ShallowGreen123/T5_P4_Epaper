
#include "ft5536.h"
#include <Arduino.h>
#include "Wire.h"

#define FT5536_I2C_ADDR 0x38

#define FTS_CMD_START_DELAY 12

#define FTS_INFO(fmt, ...) printf("[FTS/I]%s:" fmt "\n", __func__, ##__VA_ARGS__)
#define FTS_ERROR(fmt, ...) printf("[FTS/E]%s:" fmt "\n", __func__, ##__VA_ARGS__)
#define FTS_DEBUG(fmt, ...) printf("[FTS/D]%s:" fmt "\n", __func__, ##__VA_ARGS__)

void ft5536_delay_ms(int x) { delay(x); }

bool i2cReadBytes(uint8_t reg, uint8_t *dest, uint8_t count)
{
    Wire.beginTransmission(FT5536_I2C_ADDR);
    Wire.write(reg);
    Wire.endTransmission(true);

    Wire.requestFrom(FT5536_I2C_ADDR, count);
    for (int i = 0; i < count; i++)
    {
        dest[i] = Wire.read();
    }
    return true;
}

bool i2cWriteBytes(uint8_t reg, uint8_t *src, uint8_t count)
{
    Wire.beginTransmission(FT5536_I2C_ADDR);
    Wire.write(reg);
    for (int i = 0; i < count; i++)
    {
        Wire.write(src[i]);
    }
    Wire.endTransmission(true);
    return true;
}

int ft5536_read(uint8_t addr, uint8_t *data, uint16_t datalen)
{
    int ret = 0;
    int i = 0;

    if (!data || !datalen)
    {
        return -1;
    }

    i2cReadBytes(addr, data, datalen);

    return ret;
}

int ft5536_read_reg(uint8_t addr, uint8_t *value)
{
    return ft5536_read(addr, value, 1);
}

int ft5536_write(uint8_t addr, uint8_t *data, uint16_t datalen)
{
    int ret = 0;
    uint8_t txbuf[256] = {0};
    uint16_t txlen = 0;
    int i = 0;

    if (datalen >= 256)
    {
        return -1;
    }

    i2cWriteBytes(addr, data, datalen);

    return ret;
}

int ft5536_write_reg(uint8_t addr, uint8_t value)
{
    return ft5536_write(addr, &value, 1);
}

int ft5536_check_id(void)
{
    int ret = 0;
    int cnt = 0;
    uint8_t chip_id[2] = {0};

    /*delay 200ms,wait fw*/
    ft5536_delay_ms(200);

    /*get chip id*/
    ft5536_read_reg(FTS_REG_CHIP_ID, &chip_id[0]);
    ft5536_read_reg(FTS_REG_CHIP_ID2, &chip_id[1]);
    if ((FTS_CHIP_IDH == chip_id[0]) && (FTS_CHIP_IDL == chip_id[1]))
    {
        Serial.printf("get ic information, chip id = 0x%02x%02x\n", chip_id[0], chip_id[1]);
        return 0;
    }

    /*get boot id*/
    Serial.printf("fw is invalid, need read boot id\t0x%x%x\n", chip_id[0], chip_id[1]);
    // fts_hw_reset(15);
    ret = ft5536_write_reg(0x55, 0xAA);
    if (ret < 0)
    {
        Serial.printf("start cmd write fail\n");
        return ret;
    }

    ft5536_delay_ms(FTS_CMD_START_DELAY);
    ft5536_read(FTS_CMD_READ_ID, chip_id, 2);
    if ((ret == 0) && ((FTS_CHIP_IDH == chip_id[0]) && (FTS_CHIP_IDL == chip_id[1])))
    {
        Serial.printf("get ic information, boot id = 0x%02x%02x\n", chip_id[0], chip_id[1]);
        ret = 0;
    }
    else
    {
        Serial.printf("read boot id fail,read:0x%02x%02x\n", chip_id[0], chip_id[1]);
        return -1;
    }
    return ret;
}

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

static void log_touch_buf(uint8_t *buf, uint32_t buflen)
{
    int i = 0;
    int count = 0;
    char tmpbuf[512] = {0};

    for (i = 0; i < buflen; i++)
    {
        count += snprintf(tmpbuf + count, 1024 - count, "%02X,", buf[i]);
        if (count >= 1024)
            break;
    }
    Serial.printf("point buffer:%s\n", tmpbuf);
}

static void log_touch_info(struct fts_ts_event *events, uint8_t event_nums)
{
    uint8_t i = 0;

    for (i = 0; i < event_nums; i++)
    {
        Serial.printf("[%d][%d][%d,%d,%d]%d\n", events[i].id, events[i].type, events[i].x,
                      events[i].y, events[i].p, events[i].area);
    }
}

int fts_touch_get_xy(uint16_t * x, uint16_t * y)
{
    int ret = 0;
    uint8_t i = 0;
    uint8_t base = 0;
    uint8_t regaddr = 0x01;
    uint8_t buf[MAX_LEN_TOUCH_INFO]; /*A maximum of two points are supported*/
    uint8_t point_num = 0;
    uint8_t point_id = 0;

    /*read touch information from reg0x01*/
    ret = ft5536_read(regaddr, buf, MAX_LEN_TOUCH_INFO);
    if (ret < 0)
    {
        Serial.println("Read touch information from reg0x01 fails");
        return -1;
    }

    /*parse touch information based on register map*/
    point_num = buf[1] & 0x0F;
    if (point_num > FTS_MAX_POINTS_SUPPORT)
    {
        // Serial.printf("invalid point_num(%d)\n", point_num);
        return -1;
    }

    for (i = 0; i < MAX_POINTS_TOUCH_TRACE; i++)
    {
        base = 2 + i * 6;
        point_id = buf[base + 2] >> 4;
        if (point_id >= MAX_POINTS_TOUCH_TRACE)
        {
            break;
        }

        *x = ((buf[base] & 0x0F) << 8) + buf[base + 1];
        *y = ((buf[base + 2] & 0x0F) << 8) + buf[base + 3];
        break;
    }
    return 0;
}

bool fts_touch_is_pressed(void)
{
    uint8_t buf[2] = {0};
    ft5536_read_reg(0x02, &buf[0]);
    if ((buf[0] & 0x0F) > 0 && (buf[0] != 0xFF))
    {
        return true;
    }
    return false;
}

int fts_touch_process(void)
{
    int ret = 0;
    uint8_t i = 0;
    uint8_t base = 0;
    uint8_t regaddr = 0x01;
    uint8_t buf[MAX_LEN_TOUCH_INFO]; /*A maximum of two points are supported*/
    uint8_t point_num = 0;
    uint8_t touch_event_nums = 0;
    uint8_t point_id = 0;
    struct fts_ts_event events[FTS_MAX_POINTS_SUPPORT]; /* multi-touch */

    /*read touch information from reg0x01*/
    ret = ft5536_read(regaddr, buf, MAX_LEN_TOUCH_INFO);
    if (ret < 0)
    {
        Serial.println("Read touch information from reg0x01 fails");
        return ret;
    }

    /*parse touch information based on register map*/
    memset(events, 0xFF, sizeof(struct fts_ts_event) * FTS_MAX_POINTS_SUPPORT);
    point_num = buf[1] & 0x0F;
    if (point_num > FTS_MAX_POINTS_SUPPORT)
    {
        Serial.printf("invalid point_num(%d)\n", point_num);
        return -1;
    }
    Serial.printf("point_num(%d)\n", point_num);

    for (i = 0; i < MAX_POINTS_TOUCH_TRACE; i++)
    {
        base = 2 + i * 6;
        point_id = buf[base + 2] >> 4;
        if (point_id >= MAX_POINTS_TOUCH_TRACE)
        {
            break;
        }

        events[i].x = ((buf[base] & 0x0F) << 8) + buf[base + 1];
        events[i].y = ((buf[base + 2] & 0x0F) << 8) + buf[base + 3];
        events[i].id = point_id;
        events[i].type = (buf[base] >> 6) & 0x03;
        events[i].p = buf[base + 4];
        events[i].area = buf[base + 5];
        if (((events[i].type == 0) || (events[i].type == 2)) && (point_num == 0))
        {
            Serial.println("abnormal touch data from fw");
            return -2;
        }
        Serial.printf("[%d][%d][%d,%d,%d]%d\n", events[i].id, events[i].type, events[i].x,
                      events[i].y, events[i].p, events[i].area);

        touch_event_nums++;
    }

    if (touch_event_nums == 0)
    {
        Serial.printf("no touch point information(%02x)\n", buf[1]);
        return -3;
    }


    return 0;
}
