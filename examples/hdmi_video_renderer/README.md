| Supported Targets | ESP32-P4 |
| ----------------- | -------- |

# HDMI Video Renderer

This example demonstrates how to display a video in MP4/AVI format on an HDMI monitor from the
LILYGO T5-P4 E-Paper board through its on-board LT8912B HDMI bridge path.

## How to use the example

## ESP-IDF Required

### Hardware Required

* A LILYGO T5-P4 E-Paper board.
* An HDMI monitor connected to the board's LT8912B HDMI output path.
* A microSD card that stores MP4 videos.
* A USB-C cable for power supply and flashing.
* This project now uses the shared `components/t5_p4_board` board layer instead of the
  `ESP32-P4-Function-EV-Board` BSP.

Board-specific paths used by this example:

* Shared I2C bus: `SDA=7`, `SCL=8`
* HDMI power/reset through PCA9535: `IO10=1V8_EN`, `IO7=HDMI_EN`, `IO6=HDMI_RST`
* SD card over SPI: `MISO=44`, `SCK=45`, `MOSI=46`, `CS=47`
* ES8311 I2S: `MCLK=43`, `BCLK=42`, `LRCK=40`, `DOUT=39`, `DIN=41`

### Configure the Project

Run `idf.py menuconfig` and navigate to `HDMI MP4 Player Configuration` menu.

- Navigate to `Video File Configuration` submenu
- Set the `Video File Name` to match your video file name stored in the SD card
- Default value is `test_video.mp4`
- Make sure the file name matches exactly, including the `.mp4` or `.avi` extension

### Test Video Downloads

This repository provides a test MP4 file encoded with MJPEG video and AAC audio for testing and compatibility purposes.

- **Video Codec**: MJPEG  
- **Audio Codec**: AAC  
- **Frame Rate**: 20fps(RGB888)
- **File Format**: `.mp4`  
- **Download Link**: [test_video.mp4](https://dl.espressif.com/AE/esp-dev-kits/test_video.mp4)

This test file is useful for validating MJPEG decoding, AAC audio support, and embedded system playback compatibility.

### Build and Flash

Run `idf.py set-target esp32p4` to select the target chip.

Run `idf.py -p PORT build flash monitor` to build, flash and monitor the project. A fancy animation will show up on the LCD as expected.

The first time you run `idf.py` for the example will cost extra time as the build system needs to address the component dependencies and downloads the missing components from registry into `managed_components` folder.

The example is validated for the T5-P4 HDMI path with `CONFIG_BSP_LCD_TYPE_HDMI=y`,
`CONFIG_BSP_LCD_COLOR_FORMAT_RGB888=y`, and default `1280x720` output.

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

## Troubleshooting

For any technical queries, please open an [issue](https://github.com/espressif/esp-iot-solution/issues) on GitHub. We will get back to you soon.

## Important Notes

### Video Format Requirements

1. **MP4 Container Format**
   - Currently only supports MP4 files with MJPEG video encoding
   - Other video codecs (H.264, H.265, etc.) are not supported at this time
   - Audio tracks in MP4 files are supported

2. **Video Resolution and Format**
   - For YUV420 format videos:
     * Width must be divisible by 16
     * Height must be divisible by 16
     * This alignment requirement ensures optimal performance and compatibility
   - For YUV422 format videos:
     * Width must be divisible by 16
     * Height must be divisible by 8
     * Provides better color quality than YUV420 but requires more bandwidth
   - For YUV444 format videos:
     * Width must be divisible by 8
     * Height must be divisible by 8
     * Offers the highest color quality but requires the most bandwidth

3. **Display Buffer Configuration**
   - When using LCD internal buffer mode:
     * Video resolution must exactly match the HDMI output resolution
     * This ensures optimal performance and prevents display artifacts
     * Example: If HDMI is set to 1280x720, the MP4 must also be 1280x720
   - When using external buffer mode:
     * More flexible resolution support
     * Automatic scaling is available
     * May have slightly lower performance compared to internal buffer mode

### FAQ

#### Blue Screen Flickering Issues

If you encounter blue screen flickering during video playback, this is typically caused by **insufficient PSRAM bandwidth**. Here are the recommended solutions:

1. **Use RGB565 Format**
   - If your display supports RGB565, consider switching from RGB888 to RGB565 format

2. **Optimize Video Parameters**
   - **Lower Resolution**: Reduce video resolution (e.g., from 1280x720 to 1024x768)
   - **Reduce Frame Rate**: Lower the video frame rate (e.g., from 30fps to 20fps or 15fps)
   - **Increase Compression**: Use higher JPEG compression quality to reduce frame sizes

3. **AVI Format Audio Limitations**
   - **AVI videos with audio are not recommended** due to higher bandwidth requirements
   - If playing AVI files causes blue screen flickering, **disable audio playback**
   - For audio playback, use MP4 format with AAC encoding instead

These optimizations help balance video quality with available system bandwidth to ensure stable playback.

## Video Conversion with FFmpeg

### Installation
```bash
# Windows: Download from https://ffmpeg.org/download.html
# macOS: brew install ffmpeg  
# Linux: sudo apt install ffmpeg
```

### Basic MJPEG Conversion
```bash
# Convert any video to MJPEG MP4
ffmpeg -i input.mp4 -c:v mjpeg -c:a aac output.mp4
```

### Recommended Settings

**High Quality (1280x720, RGB888 displays):**
```bash
ffmpeg -i input.mp4 -c:v mjpeg -q:v 3 -vf scale=1280:720 -r 20 -c:a aac output.mp4
```

**Balanced (1024x768, recommended):**
```bash
ffmpeg -i input.mp4 -c:v mjpeg -q:v 5 -vf scale=1024:768 -r 20 -c:a aac output.mp4
```

**Low Bandwidth (800x600, RGB565 or troubleshooting):**
```bash
ffmpeg -i input.mp4 -c:v mjpeg -q:v 8 -vf scale=800:600 -r 15 -c:a aac output.mp4
```

### Key Parameters
- `-q:v`: Quality (1-31, lower = better quality, larger file)
- `-vf scale=W:H`: Resolution adjustment
- `-r`: Frame rate (15-25 fps recommended)
- `-an`: Remove audio if causing issues
