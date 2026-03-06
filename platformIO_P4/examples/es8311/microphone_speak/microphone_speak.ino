#include "AudioBoard.h" // https://github.com/pschatzmann/arduino-audio-driver
#include "ExtensionIOXL9555.hpp"
#include <driver/i2s_std.h>

// The microphone (MIC) receives the sound and then directly plays it out through the speaker.

// XL9555
#define BOARD_I2C_SDA (7)
#define BOARD_I2C_SCL (8)

// Connected to XL9555 IO05, enable power amplifier
#define BOARD_XL9555_05_AMPLIFIER (5)

// ES8311
#define BOARD_I2C_ADDR_ES8311 (0x18)
#define BOARD_ES8311_SCL (BOARD_I2C_SCL)
#define BOARD_ES8311_SDA (BOARD_I2C_SDA)
#define BOARD_ES8311_MCLK (43)
#define BOARD_ES8311_SCLK (42)
#define BOARD_ES8311_ASDOUT (39) // Existing playback examples use this as ESP DOUT
#define BOARD_ES8311_LRCK (40)
#define BOARD_ES8311_DSDIN (41)  // Used as ESP DIN for MIC capture

static const uint32_t AUDIO_SAMPLE_RATE = 44100;
static const size_t LOOPBACK_BUFFER_BYTES = 2048;
static const int SOFTWARE_GAIN = 1; // 1: no gain

#define LOGI(fmt, ...) Serial.printf("[I][%8lu] " fmt "\n", millis(), ##__VA_ARGS__)
#define LOGE(fmt, ...) Serial.printf("[E][%8lu] " fmt "\n", millis(), ##__VA_ARGS__)

ExtensionIOXL9555 io;
DriverPins my_pins;
AudioBoard board(AudioDriverES8311, my_pins);

i2s_chan_handle_t i2s_tx_handle = nullptr;
i2s_chan_handle_t i2s_rx_handle = nullptr;

struct RuntimeStats
{
    uint32_t read_ok = 0;
    uint32_t read_timeout = 0;
    uint32_t read_err = 0;
    uint32_t write_ok = 0;
    uint32_t write_err = 0;
    uint32_t empty_read = 0;
    uint64_t bytes_read = 0;
    uint64_t bytes_written = 0;
    int16_t peak = 0;
    uint32_t last_print_ms = 0;
} stats;

static inline int16_t clamp16(int32_t x)
{
    if (x > 32767)
        return 32767;
    if (x < -32768)
        return -32768;
    return (int16_t)x;
}

bool initCodec()
{
    // Register ES8311 control bus (I2C) and audio bus (I2S) pin mapping.
    my_pins.addI2C(PinFunction::CODEC, BOARD_ES8311_SCL, BOARD_ES8311_SDA, BOARD_I2C_ADDR_ES8311);
    my_pins.addI2S(PinFunction::CODEC, BOARD_ES8311_MCLK, BOARD_ES8311_SCLK, BOARD_ES8311_LRCK,
                   BOARD_ES8311_ASDOUT, BOARD_ES8311_DSDIN);

    CodecConfig cfg;
    // ES8311 microphone input + speaker output, 16-bit/44.1kHz, standard I2S format.
    cfg.input_device = ADC_INPUT_LINE1;
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_44K;
    cfg.i2s.fmt = I2S_NORMAL;

    LOGI("ES8311 begin");
    if (!board.begin(cfg))
    {
        LOGE("ES8311 init failed");
        return false;
    }

    bool out_ok = board.setVolume(55);
    bool in_ok = board.setInputVolume(45);
    LOGI("ES8311 volume out=%s in=%s", out_ok ? "ok" : "fail", in_ok ? "ok" : "fail");
    return true;
}

bool initI2SLoopback()
{
    // Create a full-duplex I2S channel pair: one TX (speaker) and one RX (mic).
    i2s_chan_config_t chan_cfg = {};
    chan_cfg.id = I2S_NUM_0;
    chan_cfg.role = I2S_ROLE_MASTER;
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 512;
    chan_cfg.auto_clear = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, &i2s_tx_handle, &i2s_rx_handle);
    if (err != ESP_OK)
    {
        LOGE("i2s_new_channel failed: %d", (int)err);
        return false;
    }

    i2s_std_config_t std_cfg = {};
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    // GPIO routing: ESP DOUT drives codec DAC path; ESP DIN receives codec ADC path.
    std_cfg.gpio_cfg.mclk = (gpio_num_t)BOARD_ES8311_MCLK;
    std_cfg.gpio_cfg.bclk = (gpio_num_t)BOARD_ES8311_SCLK;
    std_cfg.gpio_cfg.ws = (gpio_num_t)BOARD_ES8311_LRCK;
    std_cfg.gpio_cfg.dout = (gpio_num_t)BOARD_ES8311_ASDOUT;
    std_cfg.gpio_cfg.din = (gpio_num_t)BOARD_ES8311_DSDIN;
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv = false;
    std_cfg.clk_cfg.sample_rate_hz = AUDIO_SAMPLE_RATE;
    std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_128;

    LOGI("I2S pins MCLK=%d BCLK=%d LRCK=%d DOUT=%d DIN=%d",
         BOARD_ES8311_MCLK, BOARD_ES8311_SCLK, BOARD_ES8311_LRCK,
         BOARD_ES8311_ASDOUT, BOARD_ES8311_DSDIN);

    err = i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg);
    if (err != ESP_OK)
    {
        LOGE("i2s_channel_init_std_mode TX failed: %d", (int)err);
        return false;
    }

    err = i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg);
    if (err != ESP_OK)
    {
        LOGE("i2s_channel_init_std_mode RX failed: %d", (int)err);
        return false;
    }

    err = i2s_channel_enable(i2s_tx_handle);
    if (err != ESP_OK)
    {
        LOGE("i2s_channel_enable TX failed: %d", (int)err);
        return false;
    }

    err = i2s_channel_enable(i2s_rx_handle);
    if (err != ESP_OK)
    {
        LOGE("i2s_channel_enable RX failed: %d", (int)err);
        return false;
    }

    LOGI("I2S loopback started");
    return true;
}

void printStatsEvery1s()
{
    uint32_t now = millis();
    if (now - stats.last_print_ms < 1000)
    {
        return;
    }

    LOGI("rd_ok=%lu wr_ok=%lu rd_to=%lu rd_err=%lu wr_err=%lu empty=%lu br=%llu bw=%llu peak=%d",
         (unsigned long)stats.read_ok,
         (unsigned long)stats.write_ok,
         (unsigned long)stats.read_timeout,
         (unsigned long)stats.read_err,
         (unsigned long)stats.write_err,
         (unsigned long)stats.empty_read,
         stats.bytes_read,
         stats.bytes_written,
         (int)stats.peak);

    stats.read_ok = 0;
    stats.read_timeout = 0;
    stats.read_err = 0;
    stats.write_ok = 0;
    stats.write_err = 0;
    stats.empty_read = 0;
    stats.bytes_read = 0;
    stats.bytes_written = 0;
    stats.peak = 0;
    stats.last_print_ms = now;
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    LOGI("Boot start");

    if (!io.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, XL9555_SLAVE_ADDRESS0))
    {
        while (1)
        {
            LOGE("Failed to find XL9555");
            delay(1000);
        }
    }

    io.configPort(ExtensionIOXL9555::PORT0, 0x00);
    io.configPort(ExtensionIOXL9555::PORT1, 0x00);
    io.digitalWrite(BOARD_XL9555_05_AMPLIFIER, HIGH);
    LOGI("Amplifier enabled (IO%d)", BOARD_XL9555_05_AMPLIFIER);

    if (!initCodec())
    {
        while (1)
        {
            delay(1000);
        }
    }

    if (!initI2SLoopback())
    {
        while (1)
        {
            delay(1000);
        }
    }

    stats.last_print_ms = millis();
    LOGI("Ready: speak to MIC and check 1s stats");
}

void loop()
{
    static int16_t buffer[LOOPBACK_BUFFER_BYTES / sizeof(int16_t)];

    // Read one audio chunk from MIC path (RX).
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(i2s_rx_handle, buffer, sizeof(buffer), &bytes_read, 20);

    if (err == ESP_ERR_TIMEOUT)
    {
        stats.read_timeout++;
        printStatsEvery1s();
        return;
    }

    if (err != ESP_OK)
    {
        stats.read_err++;
        printStatsEvery1s();
        return;
    }

    if (bytes_read == 0)
    {
        stats.empty_read++;
        printStatsEvery1s();
        return;
    }

    stats.read_ok++;
    stats.bytes_read += bytes_read;

    size_t samples = bytes_read / sizeof(int16_t);
    // Track peak level for logs; optional software gain is kept at 1 by default.
    for (size_t i = 0; i < samples; ++i)
    {
        int32_t v = buffer[i];
        int32_t abs_v = (v >= 0) ? v : -v;
        if (abs_v > stats.peak)
        {
            stats.peak = (int16_t)abs_v;
        }

        if (SOFTWARE_GAIN > 1)
        {
            buffer[i] = clamp16(v * SOFTWARE_GAIN);
        }
    }

    // Duplicate left channel to right to avoid channel mismatch noise artifacts.
    for (size_t i = 0; i + 1 < samples; i += 2)
    {
        int16_t s = buffer[i];
        buffer[i] = s;
        buffer[i + 1] = s;
    }

    // Push processed chunk to speaker path (TX). Partial writes are handled in a loop.
    size_t offset = 0;
    while (offset < bytes_read)
    {
        size_t bytes_written = 0;
        err = i2s_channel_write(i2s_tx_handle, ((uint8_t *)buffer) + offset, bytes_read - offset, &bytes_written, 20);
        if (err != ESP_OK || bytes_written == 0)
        {
            stats.write_err++;
            break;
        }

        offset += bytes_written;
        stats.bytes_written += bytes_written;
        stats.write_ok++;
    }

    printStatsEvery1s();
}
