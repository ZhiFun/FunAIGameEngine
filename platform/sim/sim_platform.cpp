#include "sim_platform.h"

#include <cstring>

namespace sim {
namespace {

// 与 platform/ 下各硬件后端保持一致:
//   keyboard -> platform/esp32s3keyboard  (TFT_eSPI + NV3007 428x142)
//   esp32s3  -> platform/esp32s3          (Arduino_GFX + ILI9488 480x320)
const PlatformInfo kPlatforms[] = {
    {"keyboard", "键盘实例 · NV3007 428x142",        428, 142, 2},
    {"esp32s3",  "ESP32-S3 开发板 · ILI9488 480x320", 480, 320, 1},
};

} // namespace

const PlatformInfo* platform_list(int* count) {
    if (count) *count = (int)(sizeof(kPlatforms) / sizeof(kPlatforms[0]));
    return kPlatforms;
}

const PlatformInfo* find_platform(const char* id) {
    if (id == nullptr || *id == '\0') return nullptr;
    for (const PlatformInfo& p : kPlatforms) {
        if (strcmp(p.id, id) == 0) return &p;
    }
    return nullptr;
}

const PlatformInfo* default_platform() { return &kPlatforms[0]; }

} // namespace sim
