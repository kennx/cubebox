# 闹钟功能设计（Alarm）

> 需求来源：用户 2026-11-14 指令。授权 Agent 自行决断方案；本文记录所做决策与理由。
> 实施计划：`docs/superpowers/plans/2026-11-14-alarm.md`。

## 需求（用户原话归纳）

1. 在 web 配置界面设定闹钟时间。
2. 重复模式：**一次**、**按星期几循环**（周一~周日多选，选中当日响）、**每天（永久）**；默认不响（关闭）。
3. 闹钟默认响铃 **3 分钟**（固定值，不做配置项）。
4. **KEY 按键**中断闹钟响声。
5. 闹钟声音设为**最大**音量，且**渐渐响起（渐入）**。
6. 闹钟铃声自设计一个「通用的」铃声。

## 决策总览（Approaches）

### 铃声输出通道：esp_codec_dev + ES8311（选定）

- **方案 A（选定）**：`espressif/esp_codec_dev` 组件驱动板载 ES8311（I2C 0x18）+ I2S STD 输出。理由：官方 07 示例实证该链路（I2S 引脚 MCLK=19/BCLK=20/WS=22/DOUT=23，无独立 PA GPIO）；组件内置 ES8311 寄存器时序与音量控制，比手写寄存器可靠。
- 方案 B（弃）：手写 ES8311 寄存器 init + 裸 I2S（对照 hal_power 手写 AXP2101 的先例）。ES8311 时钟树/混音配置远比电源轨复杂，风险不成比例。
- 方案 C（弃）：PWM 蜂鸣器——板上无蜂鸣器，仅有功放+扬声器焊点，不成立。

**功耗关键**：I2S 外设与 MCLK 不常开——响铃开始才建 I2S 通道/打开 codec，停止即关闭拆除（时钟设备是常开床头钟，静态功耗敏感）。

### 触发调度：gui_task 每秒轮询（选定）

设备常态清醒（gui_task 10ms 主循环常驻），`alarm_service` 在循环内做**秒级去重**的时间匹配即可，无需 RTC 定时中断（PCF85063 timer 唤醒只对深睡场景有意义，本固件不深睡）。

### 响铃 UI：专用 app_alarm（选定）

触发时 `apps_request_open("alarm")` 切到全屏响铃页（点阵大字时间 + 闪烁 "ALARM" + KEY 提示），KEY 或 3 分钟超时切回时钟。理由：复用 app 框架与生命周期交接；对照「时钟上叠提示」（无独立屏、退出逻辑纠缠时钟 app）更干净。

## 配置模型（config_model，proto 4）

新增 4 个**数值**字段（沿用现有按 key 独立验证的模式，不跨字段校验——部分更新时字段间无顺序依赖）：

| key | 类型 | 约束 | 默认 |
|-----|------|------|------|
| `alarm_mode` | 整数 | 0=关闭、1=单次、2=每周、3=每天 | 0（不响） |
| `alarm_hour` | 整数 | 0–23 | 7 |
| `alarm_min` | 整数 | 0–59 | 0 |
| `alarm_weekdays` | 整数 | 0–127 位掩码：bit0=周一 … bit6=周日 | 127（全选） |

- `alarm_weekdays == 0` 且 mode=每周：合法但任何天都不响（固件不跨字段拒绝，网页端约束至少选一天）。
- `#CFG` 回传全部 4 字段；`proto` 3→4。
- NVS keys：`alarm_mode`(u8)、`alarm_time`(u16=hour*60+min 打包)、`alarm_wd`(u8)。

## 触发语义（alarm.c 纯逻辑，主机可测）

- 每秒取 `localtime_r(time(NULL))`，匹配 `HH:MM` 且**该分钟未触发过**（按 `天序号×1440+分钟` 键去重，防同分钟重复与跨模式抖动）：
  - mode=每天：到点即响；
  - mode=每周：当日星期位为 1 才响（`tm_wday` 0=周日 → `bit=(tm_wday+6)%7`）；
  - mode=单次：到点即响，**触发即刻** `config_store_disable_alarm()` 持久化 mode→0（先落盘再响，规避响铃中断/重启导致次日再响）。
- 单次闹钟语义 = 「下一个该 HH:MM 时刻」，不存日期：今天已过该时刻则明天到点才匹配（分钟去重天然保证）。
- 响铃时长 180s（`ALARM_RING_MS`）到时自动停；**KEY 下降沿**立即停。
- 时间未同步（无 NTP 且 RTC 未恢复）时同样按系统时间判定（RTC 开机即恢复系统时间，时间总是可信来源）。

## 铃声设计（hal_audio 内合成，无音频文件）

- **链路**：16 kHz / 立体声 / 16-bit I2S STD → ES8311 DAC（单声道取左路）→ 板载功放。
- **节型**（通用「电子闹钟」哔哔声）：2000 Hz 正弦，**4 短哔**（每哔 100 ms 响 / 100 ms 停）+ 800 ms 静音，循环（周期 1.6 s）。2 kHz 处于小口径扬声器最敏感频段。
- **合成**：256 点正弦 LUT + 16.16 定点相位累积器（无逐样本浮点），20 ms 块（640 帧 ×2ch×2B=2560 B 静态缓冲）经 `esp_codec_dev_write` 阻塞写。
- **渐入**：前 30 s 增益线性 5%→100%（Q8 定点 `gain = 13 + (243*elapsed)/30000`）；30 s 后满幅直至停止。
- **音量**：`esp_codec_dev_set_out_vol(100)`（最大，用户指定）。

## hal_audio 契约

```
hal_audio_init(i2c_bus)        // app_main 组合根调用：仅记总线 + 建 I2C ctrl if（共享总线，不建新总线）
hal_audio_alarm_start()        // 懒初始化：建 I2S 通道 + codec open + 音量 100 + 起播放任务；重复调用幂等
hal_audio_alarm_stop()         // 置停止标志 → 播放任务排空自删 → codec close + I2S 删除（时钟停振省电）；幂等
```

- 播放任务：`audio_alarm` 优先级 4（低于 gui 5），栈 3 KB；`esp_codec_dev_write` 阻塞节拍天然限速。
- 初始化失败不致命（无扬声器/器件缺席时 `ESP_LOGE` 继续，闹钟照常亮屏）。

## app_alarm UI

- 全屏点阵（复用 common/dotmatrix）：上排 "ALARM"（500 ms 闪烁吸引注意）、中排 HH:MM（触发时刻，非实时跳秒——响铃页语义是「闹钟时间」）、下排 "KEY OFF"（按 KEY 关闭提示）。
- 不挂 swipe_nav（响铃屏不允许滑走；KEY/超时是唯二出口）。
- 生命周期：on_open 建屏 → on_running 500ms 相位翻转重绘（dotmatrix 差量更新局部刷新）→ on_close 切回。

## main.c 胶水（gui_task）

```
alarm_service()（插在 low_power_service 之前，响铃即活动源）
  未响铃：秒级判定 → 触发：[单次先 disable 持久化] → hal_audio_alarm_start()
          → lv_disp_trig_activity(退出低功耗) → apps_request_open("alarm") → 进入响铃态
  响铃中：KEY 下降沿 或 elapsed ≥ 180s → hal_audio_alarm_stop() → apps_request_open("clock")
```

- console 新增 **`ALARM`** 调试命令：立即强制响铃（跳过时间判定；真机验证手段，对照 WAKE 的兜底定位）。
- `#HELLO` proto→4。

## web（proto 4 卡片）

- 「时钟」页新增 **AlarmCard**（TimeCard 上方）：模式单选（关闭/单次/每周/每天）+ 时:分选择 + 周几 chips（每周模式显示，至少一天）；保存发 `setCfg({alarm_mode,alarm_hour,alarm_min,alarm_weekdays})`。
- `info.proto >= 4` 门控；device.ts 类型/mock 同步扩展验证规则。

## 非目标（Non-goals）

- 响铃时长/渐入时长/音量不可配置（用户钉死：3 min/渐入/最大）。
- 贪睡（snooze）、多闹钟、深睡定时唤醒、闹钟在 app 内的屏上编辑入口。
- 响铃中触摸不停止（用户指定仅 KEY；实现上触摸切走 app 也不停声，声音由 gui_task 状态机统一停）。

## 验证

- 主机：`tests/test_alarm.c`（触发判定各模式/分钟去重/星期位换算/渐入包络）+ `test_config_model.c` 扩展。
- 构建零警告 + web `npm run build`（tsc 严格）。
- 真机：`ALARM` 命令强制响铃验证声音/渐入/KEY 停止/自动停止；`SETCFG` 设 1 分钟后闹钟验证定时触发与单次模式自动关闭。
- **需用户验证**（Agent 无法感知）：扬声器是否有声（板为「外接 2PIN 扬声器焊点」，若未焊扬声器则无声）、渐入听感、音量是否过大失真。

## 变更记录

- **2026-11-14（用户提供铃声）：铃音改为 MP3 转换的内嵌 PCM**。原设计为固件实时合成
  2000Hz 四短哔；用户提供 `universfield-digital-alarm-clock-151927.mp3`（7.6s/44.1kHz
  立体声），转换为 16kHz/单声道/s16le PCM（峰值归一 ≈-1.9dBFS，首尾 8ms 淡化消除循环
  接缝爆音）以 `EMBED_FILES` 链入固件（`main/alarm_tone.pcm`，约 243KB）。播放循环该
  PCM，渐入包络与最大音量保持不变；合成器代码移除。
  许可备注：素材由用户提供（文件名前缀 universfield，疑为 universfield 发布的数字
  闹钟音效），内嵌分发前用户自行确认其授权允许随固件再分发。
- **2026-11-14（用户反馈）：去掉独立响铃屏**。原设计为触发时切到 app_alarm 专属界面
  （时间/闪烁 ALARM/KEY 提示三行点阵）；用户验收后要求「闹钟界面显示时钟界面就可以，
  不要单独设计一个界面」——响铃不再切换应用，仅唤醒背光 + 起停声音，屏幕停留时钟
  （或用户当前所在应用）。app_alarm 应用整体移除；本文其余部分保持原决策记录。

