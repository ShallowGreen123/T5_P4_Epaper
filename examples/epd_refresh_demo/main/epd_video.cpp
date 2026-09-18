#include "epd_video.h"

#include <algorithm>
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "epd_video";
constexpr uint32_t kTargetFps = 24;
// The T5-P4 board's existing EPD configuration uses a 40 MHz pixel clock.
// Keep the reference project's row/state-machine logic, but use the faster
// board-qualified clock so a 1440x720 panel is not limited to ~15 scans/s.
constexpr uint32_t kEpdBusHz = 40000000;
constexpr int kVcomMillivolts = -1600;
constexpr uint8_t kDrivePasses = 3;
constexpr uint8_t kDirtyPadding = 6;
constexpr size_t kStateRowBytes = EPD_VIDEO_WIDTH / 2U;
constexpr size_t kStateBufferBytes = kStateRowBytes * EPD_VIDEO_HEIGHT;
constexpr size_t kDmaRowBytes = EPD_VIDEO_WIDTH / 4U;

constexpr gpio_num_t kI2cSda = GPIO_NUM_7;
constexpr gpio_num_t kI2cScl = GPIO_NUM_8;
constexpr uint8_t kPcaAddress = 0x20;
constexpr uint8_t kTpsAddress = 0x68;
constexpr uint32_t kI2cSpeedHz = 400000;
constexpr uint8_t kPcaRegInput0 = 0x00;
constexpr uint8_t kPcaRegOutput0 = 0x02;
constexpr uint8_t kPcaRegPolarity0 = 0x04;
constexpr uint8_t kPcaRegConfig0 = 0x06;
constexpr uint8_t kPcaEpdOe = 8;
constexpr uint8_t kPcaEpdMode = 9;
constexpr uint8_t kPcaTpsPwrup = 11;
constexpr uint8_t kPcaVcomCtrl = 12;
constexpr uint8_t kPcaTpsWakeup = 13;
constexpr uint8_t kPcaTpsPowerGood = 14;
constexpr uint8_t kTpsRegEnable = 0x01;
constexpr uint8_t kTpsRegVcom1 = 0x03;
constexpr uint8_t kTpsRegPowerGood = 0x0F;
constexpr uint8_t kTpsRegRevision = 0x10;
constexpr uint8_t kTpsEnableAllRails = 0x3F;
constexpr uint8_t kTpsVcom2Reserved = 0x04;
constexpr uint8_t kTpsPanelRailsMask = 0x5A;

constexpr gpio_num_t kDummyDcGpio = GPIO_NUM_22;
constexpr gpio_num_t kWrGpio = GPIO_NUM_24;
constexpr gpio_num_t kCsGpio = GPIO_NUM_25;
constexpr gpio_num_t kLeGpio = GPIO_NUM_26;
constexpr gpio_num_t kCkvGpio = GPIO_NUM_13;
constexpr gpio_num_t kStvGpio = GPIO_NUM_48;
constexpr gpio_num_t kDataGpios[8] = {
    GPIO_NUM_27, GPIO_NUM_28, GPIO_NUM_29, GPIO_NUM_30,
    GPIO_NUM_31, GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_34,
};
constexpr uint8_t kResetCounterMask[4] = {0xFC, 0xE0, 0x1C, 0x00};

i2c_master_bus_handle_t g_i2c_bus = nullptr;
i2c_master_dev_handle_t g_pca = nullptr;
i2c_master_dev_handle_t g_tps = nullptr;
uint8_t g_pca_outputs[2] = {0x00, 0x00};
uint8_t g_pca_config[2] = {0xFF, 0xFF};
esp_lcd_i80_bus_handle_t g_i80_bus = nullptr;
esp_lcd_panel_io_handle_t g_panel_io = nullptr;
TaskHandle_t g_scan_task = nullptr;
portMUX_TYPE g_buffer_lock = portMUX_INITIALIZER_UNLOCKED;

uint8_t *g_framebuffers[2] = {nullptr, nullptr};
uint8_t *g_state_buffer = nullptr;
uint8_t *g_dma_rows[2] = {nullptr, nullptr};
uint8_t *g_blank_row = nullptr;
uint8_t g_row_active[EPD_VIDEO_HEIGHT] = {};
volatile bool g_running = false;
volatile bool g_dma_done = true;
volatile bool g_flip_requested = false;
volatile bool g_drive_pending = false;
volatile uint8_t g_front_index = 0;
uint32_t g_vsync_count = 0;
uint32_t g_submit_count = 0;
uint16_t g_pending_dirty_start = 0;
uint16_t g_pending_dirty_end = EPD_VIDEO_HEIGHT - 1;

bool dma_done_callback(esp_lcd_panel_io_handle_t,
                       esp_lcd_panel_io_event_data_t *, void *)
{
    g_dma_done = true;
    return false;
}

void free_buffer(uint8_t *&buffer)
{
    if (buffer != nullptr) {
        heap_caps_free(buffer);
        buffer = nullptr;
    }
}

void release_buffers()
{
    free_buffer(g_framebuffers[0]);
    free_buffer(g_framebuffers[1]);
    free_buffer(g_state_buffer);
    free_buffer(g_dma_rows[0]);
    free_buffer(g_dma_rows[1]);
    free_buffer(g_blank_row);
}

uint8_t *allocate_8bit(size_t size, bool prefer_psram)
{
    uint8_t *buffer = nullptr;
    if (prefer_psram) {
        buffer = static_cast<uint8_t *>(heap_caps_malloc(
            size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (buffer == nullptr) {
        buffer = static_cast<uint8_t *>(heap_caps_malloc(
            size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    return buffer;
}

bool allocate_video_buffers()
{
    for (size_t i = 0; i < 2; ++i) {
        g_framebuffers[i] = allocate_8bit(EPD_VIDEO_FRAME_BYTES, true);
        if (g_framebuffers[i] == nullptr) {
            ESP_LOGE(kTag, "framebuffer allocation failed");
            release_buffers();
            return false;
        }
        memset(g_framebuffers[i], 0xFF, EPD_VIDEO_FRAME_BYTES);
    }
    g_state_buffer = allocate_8bit(kStateBufferBytes, true);
    if (g_state_buffer == nullptr) {
        ESP_LOGE(kTag, "state buffer allocation failed");
        release_buffers();
        return false;
    }
    memset(g_state_buffer, 0, kStateBufferBytes);
    for (size_t i = 0; i < 2; ++i) {
        g_dma_rows[i] = static_cast<uint8_t *>(esp_lcd_i80_alloc_draw_buffer(
            g_panel_io, kDmaRowBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        if (g_dma_rows[i] == nullptr) {
            ESP_LOGE(kTag, "DMA row allocation failed");
            release_buffers();
            return false;
        }
        memset(g_dma_rows[i], 0, kDmaRowBytes);
    }
    g_blank_row = static_cast<uint8_t *>(esp_lcd_i80_alloc_draw_buffer(
        g_panel_io, kDmaRowBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (g_blank_row == nullptr) {
        ESP_LOGE(kTag, "blank DMA row allocation failed");
        release_buffers();
        return false;
    }
    memset(g_blank_row, 0, kDmaRowBytes);
    return true;
}

esp_err_t pca_write_cached_state()
{
    uint8_t output_data[3] = {kPcaRegOutput0, g_pca_outputs[0], g_pca_outputs[1]};
    uint8_t config_data[3] = {kPcaRegConfig0, g_pca_config[0], g_pca_config[1]};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(g_pca, output_data, sizeof(output_data), 100),
                        kTag, "write PCA outputs failed");
    return i2c_master_transmit(g_pca, config_data, sizeof(config_data), 100);
}

esp_err_t pca_set_level(uint8_t io_num, bool high)
{
    ESP_RETURN_ON_FALSE(io_num < 16, ESP_ERR_INVALID_ARG, kTag,
                        "invalid PCA IO %u", io_num);
    const uint8_t port = io_num / 8U;
    const uint8_t mask = static_cast<uint8_t>(1U << (io_num % 8U));
    if (high) {
        g_pca_outputs[port] |= mask;
    } else {
        g_pca_outputs[port] &= static_cast<uint8_t>(~mask);
    }
    g_pca_config[port] &= static_cast<uint8_t>(~mask);
    return pca_write_cached_state();
}

esp_err_t pca_get_level(uint8_t io_num, bool *high)
{
    ESP_RETURN_ON_FALSE(io_num < 16 && high != nullptr, ESP_ERR_INVALID_ARG,
                        kTag, "invalid PCA input");
    uint8_t input_data[2] = {};
    uint8_t reg = kPcaRegInput0;
    ESP_RETURN_ON_ERROR(
        i2c_master_transmit_receive(g_pca, &reg, 1, input_data, sizeof(input_data), 100),
        kTag, "read PCA inputs failed");
    *high = (input_data[io_num / 8U] & (1U << (io_num % 8U))) != 0;
    return ESP_OK;
}

esp_err_t tps_write(uint8_t reg, const uint8_t *data, size_t size)
{
    uint8_t buffer[4] = {reg, 0, 0, 0};
    ESP_RETURN_ON_FALSE(size <= sizeof(buffer) - 1U, ESP_ERR_INVALID_ARG,
                        kTag, "TPS write too long");
    if (size != 0) {
        memcpy(&buffer[1], data, size);
    }
    return i2c_master_transmit(g_tps, buffer, size + 1U, 100);
}

esp_err_t tps_read_u8(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(g_tps, &reg, 1, value, 1, 100);
}

esp_err_t initialize_control_bus()
{
    if (g_i2c_bus != nullptr) {
        return ESP_OK;
    }
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.sda_io_num = kI2cSda;
    bus_config.scl_io_num = kI2cScl;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &g_i2c_bus),
                        kTag, "create I2C bus failed");

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.scl_speed_hz = kI2cSpeedHz;
    device_config.device_address = kPcaAddress;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_i2c_bus, &device_config, &g_pca),
                        kTag, "add PCA9535 failed");
    ESP_RETURN_ON_ERROR(i2c_master_probe(g_i2c_bus, kPcaAddress, 100),
                        kTag, "PCA9535 probe failed");

    device_config.device_address = kTpsAddress;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_i2c_bus, &device_config, &g_tps),
                        kTag, "add TPS65185x failed");

    const uint8_t polarity[3] = {kPcaRegPolarity0, 0x00, 0x00};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(g_pca, polarity, sizeof(polarity), 100),
                        kTag, "clear PCA polarity failed");
    return pca_write_cached_state();
}

esp_err_t wait_pca_power_good(uint32_t timeout_ms)
{
    for (uint32_t elapsed = 0; elapsed < timeout_ms; ++elapsed) {
        bool power_good = false;
        ESP_RETURN_ON_ERROR(pca_get_level(kPcaTpsPowerGood, &power_good),
                            kTag, "read PCA power-good failed");
        if (power_good) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t wait_tps_power_good(uint32_t timeout_ms)
{
    uint8_t power_good = 0;
    for (uint32_t elapsed = 0; elapsed < timeout_ms; ++elapsed) {
        ESP_RETURN_ON_ERROR(tps_read_u8(kTpsRegPowerGood, &power_good),
                            kTag, "read TPS power-good failed");
        if ((power_good & kTpsPanelRailsMask) == kTpsPanelRailsMask) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGE(kTag, "TPS rails timeout, PG=0x%02X", power_good);
    return ESP_ERR_TIMEOUT;
}

esp_err_t panel_power_on()
{
    ESP_RETURN_ON_ERROR(pca_set_level(kPcaEpdOe, false), kTag, "disable EPD OE failed");
    ESP_RETURN_ON_ERROR(pca_set_level(kPcaEpdMode, true), kTag, "set EPD mode failed");
    ESP_RETURN_ON_ERROR(pca_set_level(kPcaTpsPwrup, false), kTag, "clear PWRUP failed");
    ESP_RETURN_ON_ERROR(pca_set_level(kPcaVcomCtrl, false), kTag, "clear VCOM failed");
    ESP_RETURN_ON_ERROR(pca_set_level(kPcaTpsWakeup, true), kTag, "set WAKEUP failed");
    vTaskDelay(pdMS_TO_TICKS(2));

    uint8_t revision = 0;
    ESP_RETURN_ON_ERROR(tps_read_u8(kTpsRegRevision, &revision),
                        kTag, "read TPS revision failed");
    ESP_RETURN_ON_FALSE(revision == 0x45 || revision == 0x55 ||
                            revision == 0x65 || revision == 0x66,
                        ESP_ERR_INVALID_RESPONSE, kTag,
                        "unsupported TPS65185x revision 0x%02X", revision);

    const int raw_vcom = std::clamp((-kVcomMillivolts) / 10, 0, 0x01FF);
    const uint8_t vcom_data[2] = {
        static_cast<uint8_t>(raw_vcom & 0xFF),
        static_cast<uint8_t>(((raw_vcom >> 8) & 0x01) | kTpsVcom2Reserved),
    };
    ESP_RETURN_ON_ERROR(tps_write(kTpsRegVcom1, vcom_data, sizeof(vcom_data)),
                        kTag, "set VCOM failed");
    const uint8_t enable = kTpsEnableAllRails;
    ESP_RETURN_ON_ERROR(tps_write(kTpsRegEnable, &enable, 1),
                        kTag, "enable TPS rails failed");
    ESP_RETURN_ON_ERROR(pca_set_level(kPcaTpsPwrup, true), kTag, "set PWRUP failed");
    ESP_RETURN_ON_ERROR(wait_pca_power_good(400), kTag, "PCA power-good timeout");
    ESP_RETURN_ON_ERROR(wait_tps_power_good(400), kTag, "TPS power-good timeout");
    ESP_RETURN_ON_ERROR(pca_set_level(kPcaVcomCtrl, true), kTag, "enable VCOM failed");
    vTaskDelay(pdMS_TO_TICKS(1));
    ESP_RETURN_ON_ERROR(pca_set_level(kPcaEpdOe, true), kTag, "enable EPD OE failed");
    ESP_LOGI(kTag, "TPS65185x ready: REVID=0x%02X VCOM=%d mV", revision,
             kVcomMillivolts);
    return ESP_OK;
}

void panel_power_off()
{
    if (g_pca == nullptr) {
        return;
    }
    (void)pca_set_level(kPcaEpdOe, false);
    esp_rom_delay_us(20);
    (void)pca_set_level(kPcaVcomCtrl, false);
    (void)pca_set_level(kPcaTpsPwrup, false);
    (void)pca_set_level(kPcaEpdMode, false);
    (void)pca_set_level(kPcaTpsWakeup, false);
}

void configure_idle_levels()
{
    gpio_set_level(kLeGpio, 0);
    gpio_set_level(kStvGpio, 1);
    gpio_set_level(kCkvGpio, 1);
    gpio_set_level(kCsGpio, 1);
}

bool initialize_panel_bus()
{
    gpio_config_t control_config = {};
    control_config.pin_bit_mask = (1ULL << kCsGpio) | (1ULL << kLeGpio) |
                                  (1ULL << kCkvGpio) | (1ULL << kStvGpio);
    control_config.mode = GPIO_MODE_OUTPUT;
    control_config.pull_up_en = GPIO_PULLUP_DISABLE;
    control_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    control_config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&control_config) != ESP_OK) {
        ESP_LOGE(kTag, "configure EPD control GPIOs failed");
        return false;
    }
    configure_idle_levels();

    esp_lcd_i80_bus_config_t bus_config = {};
    bus_config.dc_gpio_num = kDummyDcGpio;
    bus_config.wr_gpio_num = kWrGpio;
    bus_config.clk_src = LCD_CLK_SRC_PLL160M;
    for (size_t i = 0; i < 8; ++i) {
        bus_config.data_gpio_nums[i] = kDataGpios[i];
    }
    bus_config.bus_width = 8;
    bus_config.max_transfer_bytes = kDmaRowBytes;
    bus_config.dma_burst_size = 64;
    esp_err_t err = esp_lcd_new_i80_bus(&bus_config, &g_i80_bus);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "create I80 bus failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_lcd_panel_io_i80_config_t panel_config = {};
    panel_config.cs_gpio_num = kCsGpio;
    panel_config.pclk_hz = kEpdBusHz;
    panel_config.trans_queue_depth = 4;
    panel_config.on_color_trans_done = dma_done_callback;
    panel_config.lcd_cmd_bits = 8;
    panel_config.lcd_param_bits = 8;
    panel_config.dc_levels.dc_idle_level = 0;
    panel_config.dc_levels.dc_cmd_level = 0;
    panel_config.dc_levels.dc_dummy_level = 0;
    panel_config.dc_levels.dc_data_level = 1;
    panel_config.flags.cs_active_high = 0;
    panel_config.flags.reverse_color_bits = 0;
    panel_config.flags.swap_color_bytes = 0;
    panel_config.flags.pclk_active_neg = 0;
    panel_config.flags.pclk_idle_low = 0;
    err = esp_lcd_new_panel_io_i80(g_i80_bus, &panel_config, &g_panel_io);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "create I80 panel IO failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void row_control_start()
{
    gpio_set_level(kCkvGpio, 1);
    esp_rom_delay_us(7);
    gpio_set_level(kStvGpio, 0);
    esp_rom_delay_us(10);
    gpio_set_level(kCkvGpio, 0);
    gpio_set_level(kCkvGpio, 1);
    esp_rom_delay_us(8);
    gpio_set_level(kStvGpio, 1);
    esp_rom_delay_us(10);
    gpio_set_level(kCkvGpio, 0);
    for (uint8_t i = 0; i < 3; ++i) {
        gpio_set_level(kCkvGpio, 1);
        esp_rom_delay_us(18);
        gpio_set_level(kCkvGpio, 0);
    }
    gpio_set_level(kCkvGpio, 1);
}

void row_control_step()
{
    gpio_set_level(kCkvGpio, 0);
    gpio_set_level(kLeGpio, 1);
    gpio_set_level(kLeGpio, 0);
}

void wait_for_dma()
{
    while (!g_dma_done) {
        esp_rom_delay_us(1);
    }
}

bool send_row(uint8_t *row_data, bool first_row)
{
    wait_for_dma();
    if (!first_row) {
        row_control_step();
    }
    g_dma_done = false;
    gpio_set_level(kCkvGpio, 1);
    const esp_err_t err =
        esp_lcd_panel_io_tx_color(g_panel_io, -1, row_data, kDmaRowBytes);
    if (err != ESP_OK) {
        g_dma_done = true;
        ESP_LOGE(kTag, "row DMA failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void sanitize_dirty_region(uint16_t dirty_y, uint16_t dirty_height,
                           uint16_t *row_start, uint16_t *row_end)
{
    if (dirty_height == 0) {
        *row_start = 0;
        *row_end = EPD_VIDEO_HEIGHT - 1;
        return;
    }
    const uint16_t first = std::min<uint16_t>(dirty_y, EPD_VIDEO_HEIGHT - 1);
    const uint32_t unclamped_last = static_cast<uint32_t>(dirty_y) + dirty_height - 1U;
    const uint16_t last = static_cast<uint16_t>(
        std::min<uint32_t>(unclamped_last, EPD_VIDEO_HEIGHT - 1U));
    *row_start = (first > kDirtyPadding) ? first - kDirtyPadding : 0;
    *row_end = std::min<uint16_t>(last + kDirtyPadding, EPD_VIDEO_HEIGHT - 1);
}

void mark_rows_active(uint16_t row_start, uint16_t row_end)
{
    for (uint16_t row = row_start; row <= row_end; ++row) {
        g_row_active[row] = 1;
    }
}

uint8_t reverse_bits(uint8_t value)
{
    value = static_cast<uint8_t>((value >> 4) | (value << 4));
    value = static_cast<uint8_t>(((value & 0xCCU) >> 2) |
                                 ((value & 0x33U) << 2));
    return static_cast<uint8_t>(((value & 0xAAU) >> 1) |
                                ((value & 0x55U) << 1));
}

bool build_active_row(const uint8_t *frame, uint16_t row, uint8_t *destination)
{
    const uint8_t *source_row =
        frame + static_cast<size_t>(row) * EPD_VIDEO_ROW_BYTES;
    uint8_t *state_ptr = g_state_buffer + static_cast<size_t>(row) * kStateRowBytes;
    uint8_t *write_ptr = destination;
    bool needs_more_drive = false;

    for (size_t source_byte = 0; source_byte < EPD_VIDEO_ROW_BYTES; ++source_byte) {
        uint8_t incoming_pixels = reverse_bits(
            source_row[EPD_VIDEO_ROW_BYTES - 1U - source_byte]);
        for (uint8_t output_byte = 0; output_byte < 2; ++output_byte) {
            uint8_t packed_drive = 0;
            for (uint8_t pair = 0; pair < 2; ++pair) {
                uint8_t state = *state_ptr;
                const uint8_t direction = incoming_pixels >> 6;
                const uint8_t difference = static_cast<uint8_t>((state ^ direction) & 0x03U);
                state &= kResetCounterMask[difference];
                state |= direction;

                packed_drive <<= 4;
                packed_drive |= (state & 0x80U)
                                    ? 0x0U
                                    : ((direction & 0x02U) ? 0x4U : 0x8U);
                packed_drive |= (state & 0x10U)
                                    ? 0x0U
                                    : ((direction & 0x01U) ? 0x1U : 0x2U);

                const uint8_t increment = static_cast<uint8_t>(((~state) >> 2) & 0x24U);
                state = static_cast<uint8_t>(state + increment);
                if (((state >> 5) & 0x07U) >= kDrivePasses) {
                    state |= 0x80U;
                }
                if (((state >> 2) & 0x07U) >= kDrivePasses) {
                    state |= 0x10U;
                }
                needs_more_drive = needs_more_drive || ((state & 0x90U) != 0x90U);
                *state_ptr++ = state;
                incoming_pixels <<= 2;
            }
            *write_ptr++ = packed_drive;
        }
    }
    g_row_active[row] = needs_more_drive ? 1U : 0U;
    return needs_more_drive;
}

uint8_t *prepare_scan_row(const uint8_t *frame, uint16_t row, uint8_t dma_index,
                          uint32_t *processed_rows, uint32_t *continuing_rows)
{
    if (g_row_active[row] == 0) {
        return g_blank_row;
    }
    ++(*processed_rows);
    if (build_active_row(frame, row, g_dma_rows[dma_index])) {
        ++(*continuing_rows);
    }
    return g_dma_rows[dma_index];
}

void sleep_to_target_frame(int64_t frame_start_us)
{
    const int64_t target_us = 1000000LL / kTargetFps;
    while (true) {
        const int64_t remaining = target_us - (esp_timer_get_time() - frame_start_us);
        if (remaining <= 0) {
            // A full T5-P4 scan can take longer than the nominal frame period.
            // Yield once in that case so CPU1's idle task can feed the task
            // watchdog; a busy scan must not monopolize the core indefinitely.
            vTaskDelay(1);
            return;
        }
        if (remaining > 2000) {
            vTaskDelay(1);
        } else {
            esp_rom_delay_us(static_cast<uint32_t>(remaining));
            return;
        }
    }
}

void scan_task(void *)
{
    ESP_LOGI(kTag, "scan task: core=%d target=%lu fps %ux%u I80=%.1f MHz",
             xPortGetCoreID(), static_cast<unsigned long>(kTargetFps),
             EPD_VIDEO_WIDTH, EPD_VIDEO_HEIGHT, kEpdBusHz / 1000000.0);
    int64_t log_window_start = esp_timer_get_time();
    uint64_t scan_time_us = 0;
    uint32_t scan_frames = 0;
    uint32_t last_submit_count = 0;

    while (g_running) {
        const int64_t frame_start = esp_timer_get_time();
        uint8_t front_index = 0;
        uint16_t dirty_start = 0;
        uint16_t dirty_end = 0;
        bool applied_flip = false;

        portENTER_CRITICAL(&g_buffer_lock);
        ++g_vsync_count;
        if (g_flip_requested) {
            g_front_index ^= 1U;
            g_flip_requested = false;
            ++g_submit_count;
            dirty_start = g_pending_dirty_start;
            dirty_end = g_pending_dirty_end;
            applied_flip = true;
        }
        front_index = g_front_index;
        portEXIT_CRITICAL(&g_buffer_lock);
        if (applied_flip) {
            mark_rows_active(dirty_start, dirty_end);
        }

        const uint8_t *frame = g_framebuffers[front_index];
        uint32_t processed_rows = 0;
        uint32_t continuing_rows = 0;
        uint8_t dma_index = 0;
        row_control_start();
        uint8_t *row_data = prepare_scan_row(frame, 0, dma_index,
                                             &processed_rows, &continuing_rows);
        bool row_uses_dma = row_data != g_blank_row;
        if (!send_row(row_data, true)) {
            g_running = false;
            break;
        }

        for (uint16_t row = 1; row < EPD_VIDEO_HEIGHT; ++row) {
            const uint8_t next_dma_index =
                row_uses_dma ? static_cast<uint8_t>(dma_index ^ 1U) : dma_index;
            uint8_t *next_row = prepare_scan_row(frame, row, next_dma_index,
                                                 &processed_rows, &continuing_rows);
            const bool next_uses_dma = next_row != g_blank_row;
            if (!send_row(next_row, false)) {
                g_running = false;
                break;
            }
            dma_index = next_dma_index;
            row_uses_dma = next_uses_dma;
        }
        if (!g_running || !send_row(g_blank_row, false)) {
            g_running = false;
            break;
        }
        wait_for_dma();

        portENTER_CRITICAL(&g_buffer_lock);
        g_drive_pending = (continuing_rows != 0U) || g_flip_requested;
        const uint32_t submitted = g_submit_count;
        const uint32_t vsync = g_vsync_count;
        portEXIT_CRITICAL(&g_buffer_lock);

        ++scan_frames;
        scan_time_us += static_cast<uint64_t>(esp_timer_get_time() - frame_start);
        const int64_t now = esp_timer_get_time();
        if (now - log_window_start >= 1000000LL) {
            const uint32_t average_ms = static_cast<uint32_t>(
                (scan_time_us / std::max<uint32_t>(scan_frames, 1U)) / 1000ULL);
            ESP_LOGI(kTag,
                     "scan=%lu fps submit=%lu fps avg=%lu ms rows=%lu active=%lu vsync=%lu",
                     static_cast<unsigned long>(scan_frames),
                     static_cast<unsigned long>(submitted - last_submit_count),
                     static_cast<unsigned long>(average_ms),
                     static_cast<unsigned long>(processed_rows),
                     static_cast<unsigned long>(continuing_rows),
                     static_cast<unsigned long>(vsync));
            last_submit_count = submitted;
            scan_frames = 0;
            scan_time_us = 0;
            log_window_start = now;
        }
        sleep_to_target_frame(frame_start);
    }
    g_scan_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

bool epd_video_init()
{
    if (g_panel_io != nullptr) {
        return true;
    }
    if (initialize_control_bus() != ESP_OK || !initialize_panel_bus()) {
        return false;
    }
    if (!allocate_video_buffers()) {
        return false;
    }
    ESP_LOGI(kTag, "buffers: frame=%u x2 state=%u DMA=%u x3, PSRAM free=%u",
             static_cast<unsigned>(EPD_VIDEO_FRAME_BYTES),
             static_cast<unsigned>(kStateBufferBytes),
             static_cast<unsigned>(kDmaRowBytes),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return true;
}

bool epd_video_power_on()
{
    if (g_panel_io == nullptr || panel_power_on() != ESP_OK) {
        panel_power_off();
        return false;
    }
    memset(g_framebuffers[0], 0xFF, EPD_VIDEO_FRAME_BYTES);
    memset(g_framebuffers[1], 0xFF, EPD_VIDEO_FRAME_BYTES);
    memset(g_state_buffer, 0, kStateBufferBytes);
    memset(g_dma_rows[0], 0, kDmaRowBytes);
    memset(g_dma_rows[1], 0, kDmaRowBytes);
    memset(g_blank_row, 0, kDmaRowBytes);
    memset(g_row_active, 0, sizeof(g_row_active));
    configure_idle_levels();
    return true;
}

bool epd_video_start()
{
    if (g_running) {
        return true;
    }
    portENTER_CRITICAL(&g_buffer_lock);
    g_front_index = 0;
    g_flip_requested = false;
    g_drive_pending = false;
    g_vsync_count = 0;
    g_submit_count = 0;
    g_pending_dirty_start = 0;
    g_pending_dirty_end = EPD_VIDEO_HEIGHT - 1;
    portEXIT_CRITICAL(&g_buffer_lock);

    g_dma_done = true;
    g_running = true;
    const BaseType_t result = xTaskCreatePinnedToCore(
        scan_task, "epd_scan", 8192, nullptr, 3, &g_scan_task, 1);
    if (result != pdPASS) {
        g_running = false;
        ESP_LOGE(kTag, "create scan task failed");
        return false;
    }
    return true;
}

uint8_t *epd_video_get_backbuffer()
{
    uint8_t back_index = 0;
    portENTER_CRITICAL(&g_buffer_lock);
    back_index = g_front_index ^ 1U;
    portEXIT_CRITICAL(&g_buffer_lock);
    return g_framebuffers[back_index];
}

size_t epd_video_get_backbuffer_size()
{
    return EPD_VIDEO_FRAME_BYTES;
}

bool epd_video_submit(uint16_t dirty_y, uint16_t dirty_height)
{
    if (!g_running) {
        return false;
    }
    uint16_t row_start = 0;
    uint16_t row_end = 0;
    sanitize_dirty_region(dirty_y, dirty_height, &row_start, &row_end);
    bool accepted = false;
    portENTER_CRITICAL(&g_buffer_lock);
    if (!g_flip_requested) {
        g_pending_dirty_start = row_start;
        g_pending_dirty_end = row_end;
        g_flip_requested = true;
        g_drive_pending = true;
        accepted = true;
    }
    portEXIT_CRITICAL(&g_buffer_lock);
    return accepted;
}

bool epd_video_can_submit()
{
    bool ready = false;
    portENTER_CRITICAL(&g_buffer_lock);
    ready = g_running && !g_flip_requested;
    portEXIT_CRITICAL(&g_buffer_lock);
    return ready;
}

bool epd_video_submit_pending()
{
    bool pending = false;
    portENTER_CRITICAL(&g_buffer_lock);
    pending = g_flip_requested || g_drive_pending;
    portEXIT_CRITICAL(&g_buffer_lock);
    return pending;
}

uint32_t epd_video_get_vsync_count()
{
    uint32_t count = 0;
    portENTER_CRITICAL(&g_buffer_lock);
    count = g_vsync_count;
    portEXIT_CRITICAL(&g_buffer_lock);
    return count;
}

uint32_t epd_video_get_submit_count()
{
    uint32_t count = 0;
    portENTER_CRITICAL(&g_buffer_lock);
    count = g_submit_count;
    portEXIT_CRITICAL(&g_buffer_lock);
    return count;
}

void epd_video_shutdown()
{
    g_running = false;
    for (uint8_t i = 0; i < 50 && g_scan_task != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    wait_for_dma();
    configure_idle_levels();
    panel_power_off();
}
