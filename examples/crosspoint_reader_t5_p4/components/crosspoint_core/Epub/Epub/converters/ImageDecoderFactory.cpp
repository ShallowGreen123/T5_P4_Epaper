#include "ImageDecoderFactory.h"

#include <Logging.h>

ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string& imagePath)
{
    LOG_INF("IMG", "Image decode is not enabled in this example: %s", imagePath.c_str());
    return nullptr;
}

bool ImageDecoderFactory::isFormatSupported(const std::string& imagePath)
{
    (void)imagePath;
    return false;
}
