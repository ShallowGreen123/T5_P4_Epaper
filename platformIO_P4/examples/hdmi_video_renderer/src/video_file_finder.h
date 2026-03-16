#pragma once

#include <string>

enum class VideoContainerType {
    Mp4,
    Avi,
};

bool findTestVideoFile(const std::string &mountPoint, std::string &outPath, VideoContainerType &outType);

