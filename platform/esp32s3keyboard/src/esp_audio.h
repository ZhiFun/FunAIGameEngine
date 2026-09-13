#pragma once
#include "engine/Audio.h"

namespace esp_hal {

// I2S 输出后端: FreeRTOS 任务循环调 AudioMixer::mix() 再写 I2S。
class I2sBackend : public engine::AudioBackend {
public:
    bool start(engine::AudioMixer& mixer) override;
    void stop() override;

private:
    engine::AudioMixer* mixer_ = nullptr;
};

} // namespace esp_hal
