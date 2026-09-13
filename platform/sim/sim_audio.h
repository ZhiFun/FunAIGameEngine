#pragma once
#include "engine/Audio.h"

#include <QIODevice>

#include <cstdint>
#include <vector>

class QAudioSink;

namespace sim {

// ============================================================================
// 拉式数据源: Qt 音频线程主动调 readData(), 我们从 AudioMixer 现取现给。
//
// 为什么不用 QTimer 推:
//   推模式靠主线程定时器喂数据, 而主线程要跑 60fps 的 LVGL + 游戏绘制
//   (每帧上万次 set_pixel), 一旦定时器被拖后, QAudioSink 内部缓冲就抽干,
//   结果就是没声音或断续。拉模式由音频线程驱动, 渲染再重也不会把声音饿死。
//
// 故意不加 Q_OBJECT: 只需要虚函数回调, 不用信号槽, 免去 moc 依赖。
// ============================================================================
class MixerDevice : public QIODevice {
public:
    MixerDevice(engine::AudioMixer& mixer, int inRate, int outRate, int outCh);

    qint64 readData(char* data, qint64 maxSize) override;
    qint64 writeData(const char* data, qint64 maxSize) override;
    qint64 bytesAvailable() const override;

private:
    engine::AudioMixer& mixer_;
    int in_rate_  = 22050;      // 混音器采样率
    int out_rate_ = 22050;      // 设备采样率
    int out_ch_   = 2;          // 设备声道数
    std::vector<int16_t> src_;  // 重采样时的中间缓冲
};

class AudioBackend : public engine::AudioBackend {
public:
    bool start(engine::AudioMixer& mixer) override;
    void stop() override;

private:
    engine::AudioMixer* mixer_ = nullptr;
    MixerDevice* dev_  = nullptr;
    QAudioSink*  sink_ = nullptr;
};

} // namespace sim
