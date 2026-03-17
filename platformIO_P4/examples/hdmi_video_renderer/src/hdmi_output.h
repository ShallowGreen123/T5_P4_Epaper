#pragma once

#include <stdint.h>

#include "esp_ldo_regulator.h"

#include "hdmi_config.h"

struct HdmiFramebuffers {
    void *fb[3];
    int count;
};

class HdmiOutput {
public:
    bool begin();
    HdmiFramebuffers framebuffers() const;
    bool present(void *fb);

private:
    bool initIoExpander();
    bool initLt8912();
    bool initDsiPhyPower();
    bool initDsi();

private:
    HdmiFramebuffers fbs_{};
    void *panel_{};
    void *dsi_bus_{};
    void *i2c_bus_{};
    void *lt8912_io_main_{};
    void *lt8912_io_cec_{};
    void *lt8912_io_avi_{};
    void *refresh_sem_{};
    esp_ldo_channel_handle_t dsi_phy_ldo_{};
    uint8_t lt8912_addr_{HDMI_LT8912_I2C_ADDR};
    bool lt8912_on_ddc_{false};
};
