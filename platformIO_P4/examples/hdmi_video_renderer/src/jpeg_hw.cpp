#include "jpeg_hw.h"

#include "esp_err.h"

#if __has_include(<driver/jpeg_decode.h>)
#include <driver/jpeg_decode.h>
#else
#error "Missing driver/jpeg_decode.h"
#endif

bool JpegHwDecoder::begin()
{
    if (decoder_) {
        return true;
    }
    jpeg_decoder_handle_t dec = nullptr;
    jpeg_decode_engine_cfg_t cfg = {};
    cfg.intr_priority = 0;
    cfg.timeout_ms = 40;
    if (jpeg_new_decoder_engine(&cfg, &dec) != ESP_OK) {
        return false;
    }
    decoder_ = dec;
    return true;
}

void JpegHwDecoder::end()
{
    if (!decoder_) {
        return;
    }
    jpeg_del_decoder_engine(static_cast<jpeg_decoder_handle_t>(decoder_));
    decoder_ = nullptr;
}

bool JpegHwDecoder::decodeRgb888(const uint8_t *jpeg, size_t jpegSize, void *outPixels, size_t outSize, uint32_t *written)
{
    if (!decoder_ || !jpeg || !outPixels || jpegSize == 0 || outSize == 0) {
        return false;
    }
    jpeg_decode_cfg_t decode_cfg = {};
    decode_cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB888;
    decode_cfg.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB;
    decode_cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;

    uint32_t out_written = 0;
    esp_err_t err = jpeg_decoder_process(static_cast<jpeg_decoder_handle_t>(decoder_), &decode_cfg, jpeg, jpegSize, static_cast<uint8_t*>(outPixels),
                                         static_cast<uint32_t>(outSize), &out_written);
    if (written) {
        *written = out_written;
    }
    return err == ESP_OK;
}
