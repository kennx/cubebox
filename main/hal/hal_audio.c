/*
 * 音频输出 HAL：ES8311（esp_codec_dev 框架，I2C 0x18）+ I2S STD（16k/立体声/16bit）。
 * 链路懒建懒拆——响铃开始才建 I2S 通道与打开 codec，停止即整链拆除：
 * 时钟设备常开运行，I2S MCLK 常开有无谓静态功耗（嵌入式优先）。
 * 声音全部实时合成（256 点正弦 LUT + Q16 相位累积），无音频文件/大缓冲。
 *
 * 上下文约定：hal_audio_alarm_start/stop 仅 gui_task 调用（对照 hal_display）；
 * 播放任务只做合成与 esp_codec_dev_write，停止时自行整链拆除后自删，
 * 使建/拆链都在播放任务单一上下文内完成，与 gui_task 无竞态。
 */
#include <math.h>
#include <string.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c_types.h> /* I2C_NUM_0（实现内允许驱动头，不外泄给上层） */
#include <driver/i2c_master.h> /* 失败路径 i2c_master_probe 诊断 */
#include <driver/i2s_std.h>

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"

#include "alarm.h"
#include "hal/hal_audio.h"

#define TAG "hal_audio"

/* 板载 I2S 引脚（官方 07 示例 board_cfg.txt 的 C6_AMOLED_2_16 条目） */
#define AUD_I2S_GPIO_MCLK 19
#define AUD_I2S_GPIO_BCLK 20
#define AUD_I2S_GPIO_WS   22
#define AUD_I2S_GPIO_DOUT 23

#define AUD_SAMPLE_RATE 16000
#define AUD_CHUNK_MS    20                            /* 每块 20ms：写阻塞节拍 */
#define AUD_CHUNK_FRAMES (AUD_SAMPLE_RATE / 1000 * AUD_CHUNK_MS) /* 320 帧 */

/* 通用闹钟节型：2000Hz 四短哔（100ms 响/100ms 停）+ 800ms 静音，周期 1.6s */
#define AUD_TONE_FREQ_HZ 2000
#define AUD_TONE_PERIOD_MS 1600

/* 相位累积器：Q16 定点，一圈 = 256 LUT 点 × 65536 */
#define AUD_PHASE_ONE_CYCLE (256u * 65536u)

static i2c_master_bus_handle_t s_bus;      /* init 记录的共享总线 */
static bool s_inited;                      /* init 已调用且 ctrl_if 可用 */
static volatile bool s_stop_req;           /* gui_task 置位：播放任务看到后拆除自删 */
static volatile bool s_playing;            /* 播放任务存活标志（stop 等其清零） */

static const audio_codec_ctrl_if_t *s_ctrl_if; /* I2C 控制（共享总线上挂 ES8311 设备） */
static const audio_codec_gpio_if_t *s_gpio_if;
static esp_codec_dev_handle_t s_codec;
static i2s_chan_handle_t s_tx_chan;
static const audio_codec_data_if_t *s_data_if;

static int16_t s_sine_lut[256];            /* 满幅正弦表（首次 start 时填充） */
static bool s_lut_ready;
static int16_t s_chunk[AUD_CHUNK_FRAMES * 2]; /* 20ms 立体声块 1280B，静态避免任务栈压力 */

/* 单块合成：pattern_pos_ms = 当前块在 1.6s 周期内的起点，elapsed_ms = 响铃已历时 */
static void synth_chunk(int16_t *out, uint32_t pattern_pos_ms, uint32_t elapsed_ms)
{
    uint32_t gain = alarm_fade_gain_q8(elapsed_ms); /* 渐入包络（Q8） */
    uint32_t phase = 0;
    /* 相位步进 = freq × 一圈 / 采样率（16k/2k 恰为整除，无相位误差） */
    const uint32_t step = (uint32_t)((uint64_t)AUD_TONE_FREQ_HZ * AUD_PHASE_ONE_CYCLE / AUD_SAMPLE_RATE);
    for (int i = 0; i < AUD_CHUNK_FRAMES; i++) {
        uint32_t t = pattern_pos_ms + (uint32_t)i * 1000u / AUD_SAMPLE_RATE;
        bool beep_on = (t % 200u) < 100u && t < 700u; /* 前 4×[100ms 响,100ms 停] */
        int32_t s = beep_on ? ((int32_t)s_sine_lut[phase >> 16] * (int32_t)gain >> 8) : 0;
        *out++ = (int16_t)s;
        *out++ = (int16_t)s; /* ES8311 单声道 DAC 取左路，双声道同值 */
        phase += step;
    }
}

/* 整链拆除（建链的对偶序）：codec 关闭/删除 → data_if → I2S 通道。
 * 播放任务与建链失败路径共用。 */
static void audio_chain_teardown(void)
{
    if (s_codec != NULL) {
        esp_codec_dev_close(s_codec);
        esp_codec_dev_delete(s_codec);
        s_codec = NULL;
    }
    if (s_data_if != NULL) {
        audio_codec_delete_data_if(s_data_if);
        s_data_if = NULL;
    }
    if (s_tx_chan != NULL) {
        i2s_channel_disable(s_tx_chan); /* 未使能时返回错误，忽略 */
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
    }
}

/* 播放任务：循环合成+阻塞写；停止时整链拆除（本任务上下文）后自删 */
static void audio_alarm_task(void *arg)
{
    (void)arg;
    int64_t start_us = esp_timer_get_time();
    uint32_t elapsed_ms = 0;
    while (!s_stop_req) {
        synth_chunk(s_chunk, (uint32_t)((esp_timer_get_time() - start_us) / 1000 % AUD_TONE_PERIOD_MS),
                   elapsed_ms);
        if (esp_codec_dev_write(s_codec, s_chunk, sizeof(s_chunk)) != 0) {
            ESP_LOGE(TAG, "codec write failed, stop ringing");
            break;
        }
        elapsed_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
    }

    audio_chain_teardown();
    s_playing = false; /* 先清存活标志再自删（stop 的等待条件） */
    vTaskDelete(NULL);
}

static bool audio_chain_build(void)
{
    const char *stage = "i2s_new_channel";
    /* I2S STD：TX-only，MCLK 由主控输出（ES8311 从机） */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, &s_tx_chan, NULL) != ESP_OK) {
        goto fail;
    }
    stage = "i2s_init_std";
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUD_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUD_I2S_GPIO_MCLK,
            .bclk = AUD_I2S_GPIO_BCLK,
            .ws = AUD_I2S_GPIO_WS,
            .dout = AUD_I2S_GPIO_DOUT,
            .din = -1, /* 仅播放 */
            .invert_flags = { 0 },
        },
    };
    if (i2s_channel_init_std_mode(s_tx_chan, &std_cfg) != ESP_OK) {
        goto fail;
    }

    stage = "new_i2s_data";
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .tx_handle = s_tx_chan,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (s_data_if == NULL) {
        goto fail;
    }

    stage = "es8311_codec_new"; /* 内部经 I2C 探测/初始化，ES8311 缺席或供电异常在此失败 */
    es8311_codec_cfg_t es8311_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .ctrl_if = s_ctrl_if,
        .gpio_if = s_gpio_if,
        .pa_pin = -1,        /* 板载功放常供电，无 PA 使能 GPIO（官方 board_cfg pa:-1） */
        .master_mode = false, /* ESP32-C6 为 I2S 主机 */
        .use_mclk = true,    /* MCLK 由 I2S 输出（GPIO19） */
        .mclk_div = 256,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);
    if (codec_if == NULL) {
        goto fail;
    }

    stage = "esp_codec_dev_new";
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = s_data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    if (s_codec == NULL) {
        goto fail;
    }
    stage = "esp_codec_dev_open";
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .sample_rate = AUD_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(s_codec, &fs) != 0) {
        goto fail;
    }
    esp_codec_dev_set_out_vol(s_codec, 100); /* 用户需求：闹钟最大音量 */
    /* I2S 通道由 esp_codec_dev_open 内部 enable（platform/audio_codec_data_i2s.c），
     * 此处不再重复调用（重复 enable 返回 INVALID_STATE） */
    return true;

fail:
    ESP_LOGE(TAG, "audio chain build failed at %s", stage);
    /* ES8311 在总线应答与否：区分「器件缺席/供电异常」与「初始化序列失败」 */
    bool present = i2c_master_probe(s_bus, 0x18, pdMS_TO_TICKS(100)) == ESP_OK;
    ESP_LOGE(TAG, "ES8311 i2c probe @0x18: %s", present ? "present" : "absent");
    audio_chain_teardown();
    return false;
}

void hal_audio_init(i2c_master_bus_handle_t bus)
{
    s_bus = bus;
    if (bus == NULL) {
        ESP_LOGE(TAG, "init: null i2c bus, audio disabled");
        return;
    }

    s_gpio_if = audio_codec_new_gpio();
    /* ES8311_CODEC_DEFAULT_ADDR 为 8 位地址（0x30），ctrl 内部右移 1 得 7 位 0x18 */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_bus, /* 共享总线：只挂设备不建新总线 */
        .clock_speed_hz = 400000,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (s_ctrl_if == NULL || s_gpio_if == NULL) {
        ESP_LOGE(TAG, "init: ctrl/gpio if create failed, audio disabled");
        return;
    }
    s_inited = true;
}

bool hal_audio_alarm_start(void)
{
    if (!s_inited) {
        ESP_LOGE(TAG, "start: audio not inited");
        return false;
    }
    if (s_playing) {
        return true; /* 幂等 */
    }

    if (!s_lut_ready) {
        for (int i = 0; i < 256; i++) {
            s_sine_lut[i] = (int16_t)(32000.0 * sinf(i * 2.0f * (float)M_PI / 256.0f));
        }
        s_lut_ready = true;
    }
    if (!audio_chain_build()) {
        return false;
    }

    s_stop_req = false;
    s_playing = true;
    if (xTaskCreate(audio_alarm_task, "audio_alarm", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "start: task create failed");
        s_playing = false;
        audio_chain_teardown(); /* 建链已成功而任务未起：就地拆除 */
        return false;
    }
    ESP_LOGI(TAG, "alarm ringing start (fade %us, max vol)", ALARM_FADE_MS / 1000);
    return true;
}

void hal_audio_alarm_stop(void)
{
    if (!s_playing) {
        return; /* 幂等 */
    }
    s_stop_req = true;
    /* 等播放任务排空当前块并整链拆除（≤ 一个 20ms 块 + 拆链开销） */
    while (s_playing) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    ESP_LOGI(TAG, "alarm ringing stop");
}
