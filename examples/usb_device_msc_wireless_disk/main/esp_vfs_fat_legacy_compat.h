#pragma once

#include "esp_vfs_fat.h"

esp_err_t esp_vfs_fat_register_legacy_compat(const char *base_path, const char *fat_drive,
                                              size_t max_files, FATFS **out_fs);

// esp_tinyusb 1.7.x uses the IDF 5.x four-argument registration API.
#define esp_vfs_fat_register(...) esp_vfs_fat_register_legacy_compat(__VA_ARGS__)
