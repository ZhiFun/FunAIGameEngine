#pragma once
#include "AudioMixer.h"

namespace engine {

// 音频输出后端(平台实现): 把 AudioMixer 的样本推给扬声器。
//   sim     -> miniaudio 设备回调里调 mixer.mix(...)
//   esp32s3 -> FreeRTOS 任务循环调 mixer.mix(...) 再 i2s_write(...)
class AudioBackend {
public:
    virtual ~AudioBackend() {}
    virtual bool start(AudioMixer& mixer) = 0;   // 开始持续输出(以 mixer 为源)
    virtual void stop() = 0;
};

// 门面: 游戏只跟它打交道
class Audio {
public:
    void attach(AudioBackend* backend) { backend_ = backend; }

    // 需在 attach 之后调用; 由平台 main 在进入游戏前调用一次
    bool start();

    AudioMixer& mixer() { return mixer_; }

    void play_sfx(const Clip* c, float vol = 1.0f) { mixer_.play_sfx(c, vol); }
    void play_bgm(const Clip* c, float vol = 1.0f) { mixer_.play_bgm(c, vol); }
    void stop_bgm()                                { mixer_.stop_bgm(); }
    void set_master_volume(float v)                { mixer_.set_master_volume(v); }

private:
    AudioMixer   mixer_{22050};
    AudioBackend* backend_ = nullptr;
    bool         started_ = false;
};

} // namespace engine
