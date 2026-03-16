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
    esp_ldo_channel_handle_t dsi_phy_ldo_{};
    uint8_t lt8912_addr_{HDMI_LT8912_I2C_ADDR};
    bool lt8912_on_ddc_{false};
};
