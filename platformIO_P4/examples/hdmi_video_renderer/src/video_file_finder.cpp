#include "video_file_finder.h"

#include <sys/stat.h>

static bool fileExists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFREG) != 0;
}

bool findTestVideoFile(const std::string &mountPoint, std::string &outPath, VideoContainerType &outType)
{
    const std::string mp4 = mountPoint + "/test_video.mp4";
    const std::string avi = mountPoint + "/test_video.avi";

    if (fileExists(mp4)) {
        outPath = mp4;
        outType = VideoContainerType::Mp4;
        return true;
    }
    if (fileExists(avi)) {
        outPath = avi;
        outType = VideoContainerType::Avi;
        return true;
    }
    return false;
}

