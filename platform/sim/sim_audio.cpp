#include "sim_audio.h"

#include <QAudio>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>

#include <cstdio>

namespace sim {

// ---------------------------------------------------------------------------
// MixerDevice —— 由 Qt 音频线程调用的数据源
// ---------------------------------------------------------------------------
MixerDevice::MixerDevice(engine::AudioMixer& mixer, int inRate, int outRate, int outCh)
    : mixer_(mixer), in_rate_(inRate), out_rate_(outRate), out_ch_(outCh) {}

qint64 MixerDevice::bytesAvailable() const {
    // 拉模式: 不需要精确预知, 给一个足够大的数, Qt 会按需一次次来取
    return 1 << 20;
}

qint64 MixerDevice::writeData(const char*, qint64) {
    return 0;                     // 只读设备
}

qint64 MixerDevice::readData(char* data, qint64 maxSize) {
    const int      frameBytes = out_ch_ * (int)sizeof(int16_t);
    const qint64   outFrames  = maxSize / frameBytes;
    if (outFrames <= 0) return 0;

    // 采样率一致: 直接混进目标缓冲(零拷贝, 常见情况)
    if (in_rate_ == out_rate_) {
        mixer_.mix(reinterpret_cast<int16_t*>(data), (uint32_t)outFrames);
        return outFrames * frameBytes;
    }

    // 采样率不同: 多取 2 帧做线性插值
    const uint32_t need = (uint32_t)((double)outFrames * in_rate_ / out_rate_) + 2;
    src_.resize((size_t)need * 2);
    mixer_.mix(src_.data(), need);

    int16_t*     out  = reinterpret_cast<int16_t*>(data);
    const double step = (double)in_rate_ / (double)out_rate_;
    for (qint64 i = 0; i < outFrames; ++i) {
        const double sp = (double)i * step;
        int i0 = (int)sp;
        if (i0 >= (int)need) i0 = (int)need - 1;
        const int i1 = (i0 + 1 < (int)need) ? i0 + 1 : i0;
        const double t = sp - (double)i0;
        // 混音器输出是立体声交错, 取左声道插值(两个声道内容本来就相同)
        const int16_t a = src_[(size_t)i0 * 2];
        const int16_t b = src_[(size_t)i1 * 2];
        const int16_t s = (int16_t)(a + (b - a) * t);
        if (out_ch_ == 1) {
            out[i] = s;
        } else {
            out[i * 2]     = s;
            out[i * 2 + 1] = s;
        }
    }
    return outFrames * frameBytes;
}

// ---------------------------------------------------------------------------
// AudioBackend
// ---------------------------------------------------------------------------
bool AudioBackend::start(engine::AudioMixer& mixer) {
    mixer_ = &mixer;
    const int inRate = (int)mixer.sample_rate();

    QAudioFormat fmt;
    fmt.setSampleRate(inRate);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice out = QMediaDevices::defaultAudioOutput();
    if (out.isNull()) {
        fprintf(stderr, "[sim] 没有可用的音频输出设备, 静音运行\n");
        fflush(stderr);
        mixer_ = nullptr;
        return false;
    }

    // 很多 Windows 设备(WASAPI 共享模式)不接受 22050Hz: 退回设备首选格式,
    // 采样率不一致时由 MixerDevice 内部线性重采样。
    if (!out.isFormatSupported(fmt)) {
        QAudioFormat pref = out.preferredFormat();
        pref.setSampleFormat(QAudioFormat::Int16);
        if (pref.channelCount() < 1) pref.setChannelCount(2);
        if (!out.isFormatSupported(pref)) {
            fprintf(stderr, "[sim] 音频设备既不支持 %dHz/2ch Int16, 也不支持它的首选格式"
                            "(%dHz/%dch) —— 静音运行\n",
                    inRate, pref.sampleRate(), pref.channelCount());
            fflush(stderr);
            mixer_ = nullptr;
            return false;
        }
        fmt = pref;
        fprintf(stderr, "[sim] 音频: 设备不支持 %dHz, 改用 %dHz/%dch(内部线性重采样)\n",
                inRate, fmt.sampleRate(), fmt.channelCount());
        fflush(stderr);
    }

    dev_ = new MixerDevice(mixer, inRate, fmt.sampleRate(), fmt.channelCount());
    if (!dev_->open(QIODevice::ReadOnly)) {
        fprintf(stderr, "[sim] 混音数据源打开失败, 静音运行\n");
        fflush(stderr);
        delete dev_;
        dev_ = nullptr;
        mixer_ = nullptr;
        return false;
    }

    sink_ = new QAudioSink(out, fmt);

    // 设备状态变化直接打出来: 万一被系统中断/挂起, 日志里能看见
    QObject::connect(sink_, &QAudioSink::stateChanged, [](QAudio::State s) {
        if (s == QAudio::StoppedState) {
            fprintf(stderr, "[sim] audio state=Stopped (被系统停掉了)\n");
            fflush(stderr);
        }
    });

    sink_->setBufferSize((qsizetype)fmt.sampleRate() * fmt.channelCount() * 2 / 5);  // ~200ms
    sink_->start(dev_);          // 拉模式: 音频线程主动来 readData()

    fprintf(stderr, "[sim] 音频就绪: %dHz/%dch, 拉模式(Qt 音频线程喂数据)\n",
            fmt.sampleRate(), fmt.channelCount());
    fflush(stderr);
    return true;
}

void AudioBackend::stop() {
    if (sink_) { sink_->stop();  delete sink_;  sink_  = nullptr; }
    if (dev_)  { dev_->close();  delete dev_;   dev_   = nullptr; }
    mixer_ = nullptr;
}

} // namespace sim
