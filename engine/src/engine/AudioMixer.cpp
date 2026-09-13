#include "AudioMixer.h"
#include <cmath>
#include <algorithm>

namespace engine {

AudioMixer::AudioMixer(uint32_t sampleRate) : rate_(sampleRate ? sampleRate : 22050) {}

// 每个输出帧在 clip 里前进多少样本。素材采样率 == 输出采样率时是 1.0(与以前完全一致),
// 素材更低就 <1(零阶保持上采样, 时长/音高不变)。
float AudioMixer::step_of(const Clip* c) const {
    const uint32_t sr = (c != nullptr && c->rate > 0) ? c->rate : rate_;
    return (float)sr / (float)rate_;
}

void AudioMixer::set_master_volume(float v) {
    master_ = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void AudioMixer::play_sfx(const Clip* c, float vol) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (c == nullptr || c->frames == 0 || c->data == nullptr) return;

    // 同一段音效还在前半段时不再叠加(避免连发糊成一片)
    for (int i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].clip == c && voices_[i].pos * 2.0f < (float)c->frames) return;
    }

    int slot = -1;
    for (int i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].clip == nullptr || voices_[i].pos >= (float)voices_[i].clip->frames) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        // 没有空声部: 替换掉"快放完"的那个
        float best = 0.0f;
        for (int i = 0; i < kMaxVoices; ++i) {
            const Clip* c2 = voices_[i].clip;
            if (c2 == nullptr) { slot = i; break; }
            const float ratio = voices_[i].pos / (float)c2->frames;
            if (ratio >= best) { best = ratio; slot = i; }
        }
    }
    if (slot < 0) return;

    voices_[slot].clip = c;
    voices_[slot].pos  = 0;
    voices_[slot].vol  = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
}

void AudioMixer::play_bgm(const Clip* c, float vol) {
    std::lock_guard<std::mutex> lock(mtx_);
    // 传 nullptr 不当成"停止": 素材缺失时应该继续放当前这首, 而不是静音。
    // 真要停用 stop_bgm()。
    if (c == nullptr) return;
    bgm_     = c;
    bgm_pos_ = 0.0f;
    bgm_vol_ = vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol);
}

void AudioMixer::stop_bgm() {
    std::lock_guard<std::mutex> lock(mtx_);
    bgm_     = nullptr;
    bgm_pos_ = 0.0f;
}

void AudioMixer::mix(int16_t* out, uint32_t frames) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (uint32_t i = 0; i < frames; ++i) {
        float acc = 0.0f;

        if (bgm_ != nullptr && bgm_->frames > 0) {
            uint32_t idx = (uint32_t)bgm_pos_;
            if (idx >= bgm_->frames) { bgm_pos_ = 0.0f; idx = 0; }
            acc += (float)bgm_->data[idx] * bgm_vol_;
            bgm_pos_ += step_of(bgm_);
            if (bgm_pos_ >= (float)bgm_->frames) {
                bgm_pos_ = std::fmod(bgm_pos_, (float)bgm_->frames);   // 循环
            }
        }

        for (int v = 0; v < kMaxVoices; ++v) {
            Voice& vo = voices_[v];
            if (vo.clip == nullptr) continue;
            const uint32_t idx = (uint32_t)vo.pos;
            if (idx >= vo.clip->frames) { vo.clip = nullptr; continue; }
            acc += (float)vo.clip->data[idx] * vo.vol;
            vo.pos += step_of(vo.clip);
        }

        int32_t s = (int32_t)(acc * master_);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[2 * i]     = (int16_t)s;
        out[2 * i + 1] = (int16_t)s;
    }
}

} // namespace engine
