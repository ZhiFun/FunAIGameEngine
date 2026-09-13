// ESP32-S3 音频后端(键盘硬件实例): 单路 I2S 输出给 MAX98357。
// 引脚与键盘工程一致: I2S_NUM_1, BCLK=16, LRC=39, DOUT=38。
// (键盘的麦克风用 I2S_NUM_0, 游戏只出声, 不碰 MIC。)
#include "esp_audio.h"
#include <driver/i2s.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define AUDIO_I2S_PORT I2S_NUM_1
#define AUDIO_BCLK     16
#define AUDIO_LRC      39
#define AUDIO_DOUT     38

namespace {

engine::AudioMixer* g_mixer = nullptr;
volatile bool       g_run   = false;
TaskHandle_t        g_task  = nullptr;

void audio_task(void*) {
    constexpr int kBlock = 240;                 // 22050Hz 下约 10.9ms
    static int16_t buf[kBlock * 2];             // 交错立体声
    const uint32_t want_us = (uint32_t)kBlock * 1000000UL / g_mixer->sample_rate();
    while (g_run) {
        const uint32_t t0 = (uint32_t)esp_timer_get_time();
        g_mixer->mix(buf, kBlock);
        size_t written = 0;
        i2s_write((i2s_port_t)AUDIO_I2S_PORT, buf, sizeof(buf), &written, pdMS_TO_TICKS(100));
        const uint32_t spent = (uint32_t)esp_timer_get_time() - t0;
        if (spent < want_us) vTaskDelay(pdMS_TO_TICKS((want_us - spent) / 1000));
    }
    g_task = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

bool I2sBackend::start(engine::AudioMixer& mixer) {
    mixer_ = &mixer;
    g_mixer = &mixer;

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = mixer.sample_rate();
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 4;
    cfg.dma_buf_len          = 128;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    cfg.fixed_mclk           = I2S_PIN_NO_CHANGE;
    if (i2s_driver_install((i2s_port_t)AUDIO_I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
        mixer_ = nullptr;
        g_mixer = nullptr;
        return false;
    }

    i2s_pin_config_t pin = {};
    pin.mck_io_num   = I2S_PIN_NO_CHANGE;   // 必须显式 -1(0 会把 MCK 配到 GPIO0)
    pin.bck_io_num   = AUDIO_BCLK;
    pin.ws_io_num    = AUDIO_LRC;
    pin.data_out_num = AUDIO_DOUT;
    pin.data_in_num  = I2S_PIN_NO_CHANGE;
    i2s_set_pin((i2s_port_t)AUDIO_I2S_PORT, &pin);
    i2s_zero_dma_buffer((i2s_port_t)AUDIO_I2S_PORT);

    g_run = true;
    // 优先级与键盘主任务相当(不要更高), 避免驱动异常时饿死其它任务。
    xTaskCreatePinnedToCore(audio_task, "eng_audio", 3072, nullptr, 1, &g_task, 1);
    return true;
}

void I2sBackend::stop() {
    g_run = false;
    vTaskDelay(pdMS_TO_TICKS(60));
    i2s_driver_uninstall((i2s_port_t)AUDIO_I2S_PORT);
    mixer_ = nullptr;
    g_mixer = nullptr;
}
