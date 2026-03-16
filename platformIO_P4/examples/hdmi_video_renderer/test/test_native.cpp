#include <unity.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "hdmi_config.h"
#include "video_file_finder.h"

static std::string makeTempDir()
{
    const auto base = std::filesystem::temp_directory_path() / "pio_hdmi_video_renderer_test";
    std::filesystem::create_directories(base);
    const auto dir = base / std::to_string(std::rand());
    std::filesystem::create_directories(dir);
    return dir.string();
}

void test_sd_scan_prefers_mp4_when_both_exist()
{
    const std::string mp = makeTempDir();
    std::ofstream(mp + "/test_video.avi").put('\0');
    std::ofstream(mp + "/test_video.mp4").put('\0');

    std::string path;
    VideoContainerType type{};
    TEST_ASSERT_TRUE(findTestVideoFile(mp, path, type));
    TEST_ASSERT_EQUAL((int)VideoContainerType::Mp4, (int)type);
    TEST_ASSERT_TRUE(path.size() >= 4);
    TEST_ASSERT_TRUE(path.rfind(".mp4") == path.size() - 4);
}

void test_sd_scan_finds_avi_when_only_avi_exists()
{
    const std::string mp = makeTempDir();
    std::ofstream(mp + "/test_video.avi").put('\0');

    std::string path;
    VideoContainerType type{};
    TEST_ASSERT_TRUE(findTestVideoFile(mp, path, type));
    TEST_ASSERT_EQUAL((int)VideoContainerType::Avi, (int)type);
    TEST_ASSERT_TRUE(path.size() >= 4);
    TEST_ASSERT_TRUE(path.rfind(".avi") == path.size() - 4);
}

void test_sd_scan_returns_false_when_missing()
{
    const std::string mp = makeTempDir();
    std::string path;
    VideoContainerType type{};
    TEST_ASSERT_FALSE(findTestVideoFile(mp, path, type));
}

void test_lt8912_i2c_address_is_0x48()
{
    TEST_ASSERT_EQUAL_HEX8(0x48, HDMI_LT8912_I2C_ADDR);
}

void test_1080p30_pixel_clock_is_74_25_mhz()
{
    const double pixelClockMhz = HDMI_DPI_CLOCK_MHZ;
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 74.25, pixelClockMhz);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sd_scan_prefers_mp4_when_both_exist);
    RUN_TEST(test_sd_scan_finds_avi_when_only_avi_exists);
    RUN_TEST(test_sd_scan_returns_false_when_missing);
    RUN_TEST(test_lt8912_i2c_address_is_0x48);
    RUN_TEST(test_1080p30_pixel_clock_is_74_25_mhz);
    return UNITY_END();
}
