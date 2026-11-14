/*
 * 音频输出 HAL：ES8311（esp_codec_dev 框架，I2C 0x18）+ I2S STD（16k/立体声/16bit）。
 * 链路懒建懒拆——响铃开始才建 I2S 通道与打开 codec，停止即整链拆除：
 * 时钟设备常开运行，I2S MCLK 常开有无谓静态功耗（嵌入式优先）。
 * 铃声 = 内嵌 PCM 循环播放（main/alarm_tone.pcm，EMBED_FILES 链入固件；
 * 由用户提供的 universfield-digital-alarm-clock MP3 转换：16kHz/单声道/s16le，
 * 峰值归一至约 -1.9dBFS，首尾 8ms 淡化消除循环接缝爆音），前 30s 渐入。
 *
 * 上下文约定：hal_audio_alarm_start/stop 仅 gui_task 调用（对照 hal_display）。
 * 建链在 start（gui_task 上下文，同步完成后再创建播放任务）；拆链的归属：
 * 正常停止由播放任务自行拆除后自删（gui_task 阻塞等待其退出），建链失败/
 * 任务创建失败/任务卡死 >2s 的兜底则由 gui_task 就地拆除——各路径执行时
 * 播放任务要么尚未创建、要么已确认退出/被强杀，链路资源无并发访问。
 */
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

/* 铃音资源（CMakeLists EMBED_FILES alarm_tone.pcm → 链接器符号） */
extern const uint8_t _binary_alarm_tone_pcm_start[];
extern const uint8_t _binary_alarm_tone_pcm_end[];

static i2c_master_bus_handle_t s_bus;      /* init 记录的共享总线 */
static bool s_inited;                      /* init 已调用且 ctrl_if 可用 */
static volatile bool s_stop_req;           /* gui_task 置位：播放任务看到后拆除自删 */
static volatile bool s_playing;            /* 播放任务存活标志（stop 等其清零） */
static TaskHandle_t s_task;                /* 播放任务句柄（任务退出前自清；僵尸恢复用） */

static const audio_codec_ctrl_if_t *s_ctrl_if; /* I2C 控制（共享总线上挂 ES8311 设备） */
static const audio_codec_gpio_if_t *s_gpio_if;
static const audio_codec_if_t *s_codec_if; /* ES8311 接口（每次建链新建，拆链释放） */
static esp_codec_dev_handle_t s_codec;
static i2s_chan_handle_t s_tx_chan;
static const audio_codec_data_if_t *s_data_if;

static int16_t s_chunk[AUD_CHUNK_FRAMES * 2]; /* 20ms 立体声块 1280B，静态避免任务栈压力 */

/* 单块装填：从内嵌 PCM 取 320 帧（循环回绕），乘渐入增益后复制到双声道。
 * pos 为 PCM 内当前单声道样本下标（调用方持有，跨块推进）。 */
static void pcm_chunk_fill(int16_t *out, size_t *pos, uint32_t elapsed_ms)
{
    const int16_t *pcm = (const int16_t *)_binary_alarm_tone_pcm_start;
    const size_t total = (size_t)(_binary_alarm_tone_pcm_end - _binary_alarm_tone_pcm_start) / sizeof(int16_t);
    const int32_t gain = (int32_t)alarm_fade_gain_q8(elapsed_ms); /* 渐入包络（Q8） */
    size_t p = *pos;
    for (int i = 0; i < AUD_CHUNK_FRAMES; i++) {
        int16_t s = (int16_t)(((int32_t)pcm[p] * gain) >> 8);
        *out++ = s;
        *out++ = s; /* ES8311 单声道 DAC 取左路，双声道同值 */
        if (++p >= total) {
            p = 0; /* 铃音循环 */
        }
    }
    *pos = p;
}

/* 整链拆除（建链的对偶序）：codec 关闭/删除 → codec_if → data_if → I2S 通道。
 * 播放任务与建链失败路径共用。 */
static void audio_chain_teardown(void)
{
    if (s_codec != NULL) {
        esp_codec_dev_close(s_codec);
        esp_codec_dev_delete(s_codec);
        s_codec = NULL;
    }
    if (s_codec_if != NULL) {
        audio_codec_delete_codec_if(s_codec_if);
        s_codec_if = NULL;
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

/* 播放任务：PCM 循环装填+阻塞写；停止时整链拆除（本任务上下文）后自删 */
static void audio_alarm_task(void *arg)
{
    (void)arg;
    const int64_t start_us = esp_timer_get_time();
    uint32_t elapsed_ms = 0;
    size_t pcm_pos = 0;
    while (!s_stop_req) {
        pcm_chunk_fill(s_chunk, &pcm_pos, elapsed_ms);
        if (esp_codec_dev_write(s_codec, s_chunk, sizeof(s_chunk)) != 0) {
            ESP_LOGE(TAG, "codec write failed, stop ringing");
            break;
        }
        elapsed_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
    }

    audio_chain_teardown();
    s_playing = false; /* 先清存活标志再清句柄再自删（stop 的等待/僵尸恢复条件） */
    s_task = NULL;
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
    s_codec_if = codec_if;

    stage = "esp_codec_dev_new";
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = s_codec_if,
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
        if (s_ctrl_if != NULL) {
            audio_codec_delete_ctrl_if(s_ctrl_if);
            s_ctrl_if = NULL;
        }
        if (s_gpio_if != NULL) {
            audio_codec_delete_gpio_if(s_gpio_if);
            s_gpio_if = NULL;
        }
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

    if (!audio_chain_build()) {
        return false;
    }

    s_stop_req = false;
    s_playing = true;
    if (xTaskCreate(audio_alarm_task, "audio_alarm", 3072, NULL, 4, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "start: task create failed");
        s_playing = false;
        s_task = NULL;
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
    /* 等播放任务排空当前块并整链拆除（≤ 一个 20ms 块 + 拆链开销）。
     * 必须 vTaskDelay(1) 真阻塞：pdMS_TO_TICKS(<10ms) 在 100Hz tick 下截断为 0
     * → vTaskDelay(0)=taskYIELD 不阻塞，gui_task(优先级5) 会饿死更低优先级的
     * 播放任务(4)，死锁并触发 IDLE 看门狗（实测教训，见 plans/2026-11-14-alarm.md） */
    int wait_ms = 0;
    while (s_playing) {
        vTaskDelay(1);
        wait_ms += portTICK_PERIOD_MS;
        if (wait_ms >= 2000) {
            /* 僵尸恢复（code review 落地）：任务卡死在阻塞写（罕见，DMA/总线级
             * 故障）时强杀并就地拆链。残留 s_playing=true 会让后续 start 幂等
             * 返回成功而不重建链路——闹钟从此静音直到重启，故障被放大。
             * vTaskDelete 可能泄漏任务持有的驱动信号量，此时属灾难恢复取舍：
             * 功能可用性优先。s_playing 仍真 ⇒ 任务未执行退出序列 ⇒ 句柄有效。 */
            ESP_LOGE(TAG, "stop: playback task stuck >2s, force kill & teardown");
            vTaskDelete(s_task);
            s_task = NULL;
            audio_chain_teardown();
            s_playing = false;
            break;
        }
    }
    ESP_LOGI(TAG, "alarm ringing stop");
}
