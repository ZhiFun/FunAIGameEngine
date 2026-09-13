#pragma once
#include <stdint.h>
#include <mutex>
#include "AssetStore.h"

namespace engine {

// 平台无关的软件混音器: 一条 BGM 循环 + 最多 N 条音效声部同时出声。
// 模拟器(miniaudio 回调)与硬件(I2S 任务)都用它, 保证听感一致。
//
// 采样率: mix() 的输出恒为 rate_, 但 Clip 自带 rate_, 播放时按 clip->rate / rate_
// 的步长取样 —— 所以素材可以存成更低的采样率(比如 11025Hz)来省 SPIFFS/flash,
// 而时长与音高不变(低的那部分是零阶保持上采样)。
class AudioMixer {
public:
    explicit AudioMixer(uint32_t sampleRate = 22050);

    uint32_t sample_rate() const { return rate_; }

    void set_master_volume(float v);          // 0..1
    float master_volume() const { return master_; }

    void play_sfx(const Clip* c, float vol = 1.0f);   // 与 BGM 叠加
    void play_bgm(const Clip* c, float vol = 1.0f);   // 替换当前 BGM 并循环
    void stop_bgm();

    // 混出 frames 帧, 写入交错立体声 int16(out[2*i]=L, out[2*i+1]=R)
    void mix(int16_t* stereoInterleaved, uint32_t frames);

private:
    static constexpr int kMaxVoices = 6;
    struct Voice {
        const Clip* clip = nullptr;
        float       pos  = 0.0f;      // 单位是 clip 自己的采样(不是输出帧)
        float       vol  = 1.0f;
    };

    float step_of(const Clip* c) const;   // 每个输出帧在 clip 里前进多少

    uint32_t rate_     = 22050;
    float    master_   = 1.0f;
    // 模拟器上 mix() 跑在 Qt 音频线程, 与游戏线程的 play_* 互斥; 实机上是单任务无竞争
    std::mutex mtx_;
    const Clip* bgm_   = nullptr;
    float    bgm_pos_  = 0.0f;
    float    bgm_vol_  = 1.0f;
    Voice     voices_[kMaxVoices];
};

} // namespace engine
