# 低功耗优化设计：DFS + 自动 Light-Sleep（方案 C）

日期：2026-11-03
状态：待批准
前置：docs/features/wifi-time-sync.md（设置页/WiFi 对时/低功耗已交付并合入 main）

## 1. 意图与背景

用户目标（原话）：*"低功耗没有被唤醒的时候需要降频时钟能正常运行，只有被唤醒后才恢复正常，最好有 2 天以上的工作时间"*。

现状：60s 无触摸进入"低功耗模式"只做了**屏幕降亮度到 18%**，SoC 全程 160MHz 空转不休眠。两轮真机放电实测（[run1](../measurements/2026-11-02-baseline-discharge.csv) / [run2](../measurements/2026-11-03-baseline-run2.csv)）：

| 指标 | 实测 |
|------|------|
| 平均电流（电压模型 + 85-95% 窗口折算） | **~55mA** |
| 满电→断电续航 | **~18h（0.75 天）** |
| 两轮一致性（4.05→3.75V 窗口） | 8.20h vs 8.17h（36.5 vs 36.6 mV/h），高度可信 |

2 天目标（48h+）要求平均电流 **≤18mA**，即砍掉 ~37mA——只能来自 SoC 休眠。

### 电流预算（估算 + 实测锚定）

| 负载 | 现状 | 优化后 | 依据 |
|------|------|--------|------|
| SoC（160MHz 常开 + 外设时钟 + USB 外设） | ~30mA | **~4mA** | C6 手册 modem-sleep 160MHz 27-38mA；light-sleep 180µA + 唤醒占空比 ~10% @ ~20mA |
| AMOLED 18% 亮度点阵（~5% 像素点亮） | ~7mA | ~7mA | 面板估算：1 万点亮像素 × 2.3µA × 16% 亮度 + 驱动静态 ~3mA |
| CST9220 触摸轮询 | ~1mA | ~1mA | 芯片自动睡眠，维持 |
| ALDO 轨（麦克风等） | ~3mA | ~3mA | 本期不动 |
| AXP2101 静态 + DCDC 转换损耗 | ~3mA | ~3mA | 效率 ~90% |
| **合计** | **~44-55mA（实测吻合）** | **~18mA** | → 850-900mAh 可用 / 18mA ≈ **47-50h** |

## 2. 目标 / 非目标

**目标**
1. 电池低功耗态平均电流 ≤18mA，满电续航 ≥48h（最终以放没电实测验收）
2. USB 在位时零功能损失：全速、console/日志/烧录/调试全可用
3. 时钟走时、触摸唤醒（≤50ms 感知阈值内）、唤醒后亮度恢复正常——现有交互全部保留
4. A/B 复测方法：85-95% 电量窗口斜率从 3.6%/h 降到 **<1.2%/h**（电量计该段最平坦、两轮一致）

**非目标**
- 不做关屏/deep-sleep（与用户"屏幕最低可见度时钟继续走"需求冲突）
- 不动 WiFi 对时流程（对时只发生在用户主动操作时，天然避开睡眠态）
- 不做 ALDO 轨裁剪、触摸深睡（列入后续优化，本期不碰以控风险）

**反目标（即使技术可行也不做）**
- 牺牲触摸唤醒响应换功耗
- 插着 USB 时进入任何睡眠（会断 USB 连接，破坏开发/调试体验）

## 3. 方案对比（对话中已选定 C）

| 方案 | 机制 | 预估续航 | 结论 |
|------|------|---------|------|
| A 只降频 | esp_pm DFS 锁 80/40MHz，CPU 不睡 | ~20-22h | 达不到 2 天，否决 |
| B DFS + 自动 light-sleep | 电池态允许 tickless 自动入睡 | ~47-55h | 主收益来源 |
| **C = B + 打底优化** | B 基础上：低功耗静默日志、主循环减速、秒位局部重绘 | 55h+，留余量 | **选定** |

## 4. 技术设计

### 4.1 三态电源策略（新模块 `main/app_pm.c/h`）

sdkconfig：`CONFIG_PM_ENABLE=y` + `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`。

| 状态 | 触发条件 | esp_pm_configure | 说明 |
|------|---------|------------------|------|
| S0 USB_FAST | vbus=1 | {max=160, min=160, light_sleep=false} | 插线=现状，console/烧录/动画全速 |
| S1 BATT_ACTIVE | vbus=0 且用户在用（未进低功耗） | {max=160, min=80, light_sleep=false} | 交互态只降频不睡，保触摸跟手与动画帧率 |
| S2 BATT_IDLE | vbus=0 且 60s 无触摸（现有 s_low_power） | {max=160, min=40, light_sleep=true} | 深度省电态；max 保 160 是为了缩短每秒重绘的唤醒时长（忙时高频短跑比低频长跑省电） |

接口：`app_pm_vbus_set(bool present)`、`app_pm_lowpower_set(bool on)`、`app_pm_state_query()`。内部做状态合成与去抖：**vbus 消失需连续 3 次采样（3s）确认**才离开 S0（防 USB 接触抖动反复切）；vbus 出现**立即**切 S0。

**S2 进入联锁（WiFi interlock）**：用户发起 WiFi 对时后若 60s 不再触摸（如等扫描/等连接），idle 状态机仍会请求 S2。esp_wifi 与自动 light-sleep 官方兼容不会出错，但对时期间频繁入睡拖慢 SNTP/扫描。规则：`app_wifi_get_status()->state` 处于 SCANNING/CONNECTING/SYNCING 时暂缓进入 S2（app_pm 内部查询，终态 IDLE/DONE/FAILED 放行）。

切换驱动：
- 低功耗进入/退出：gui_task 主循环（10ms 粒度，现有 idle 状态机处）
- vbus 变化：gui_task 内每 1s 顺路读一次 AXP2101（与 RTC 读同节奏，同总线复用），插拔 1s 内响应

### 4.2 USB-Serial/JTAG 与 light-sleep 的冲突（本设计最大风险点）

已知 Espressif 行为：USB-Serial/JTAG 的 PHY 在 light-sleep 期间停时钟 → host 端断连。

- S2 仅在 vbus=0（电池态）进入 → 正常使用不受影响
- **已知取舍**：拔线后重新插回 USB 的 ≤1s 检测窗口内，host 枚举可能撞上睡眠态而失败——**重插一次即恢复**。缓解措施（都做）：
  1. vbus 1s 轮询（gui_task 顺路，I2C 一次 ~0.5ms）
  2. time_sync_task 收到任意 console 字节 → 立即请求切 S0（处理 host 已枚举成功但设备在 S1/S2 边缘的场景）
- 若实测枚举失败率高，升级方案（预留）：vbus 轮询加密到 200ms。

### 4.3 前置修复：LVGL tick 源（**必须，否则自动入睡永不发生**）

`bsp_lcd.c` 现有 2ms 周期 esp_timer 驱动 `lv_tick_inc()`。tickless 入睡要求预期空闲 ≥ `FREERTOS_IDLE_TIME_BEFORE_SLEEP`（默认 3 tick = 30ms），**2ms 到期点使该条件永远不满足——不修这个，S2 只剩 DFS 降频，功耗目标整段落空**（builder check 发现）。

修法：改用 LVGL v8 原生 `CONFIG_LV_TICK_CUSTOM=y`，`LV_TICK_CUSTOM_SYS_TIME_EXPR = ((uint32_t)(esp_timer_get_time() / 1000LL))`，删除 2ms 定时器与 `lv_tick_inc` 回调。行为等价（LVGL 内部时间戳改为按需读取），且省掉 500Hz 的定时器中断本身。

### 4.4 打底优化（仅 S2 生效）

1. **静默走时日志**：`ui_clock_tick_refresh()` 的每秒 `ESP_LOGI` 在低功耗态跳过（主循环里已经有每秒日志消耗 vprintf+console 开销）
2. **主循环减速**：gui_task `vTaskDelay` 10ms→50ms（S2 时）。indev 触摸采样率随之 ~20Hz，唤醒延迟 ≤50ms
3. **秒位局部重绘**：S2 时只 invalidate 秒位变化区域（时间行右侧 2 字符 ≈ 130×63px）；`sec==0` 时扩大到整时间行+日期行（分钟/日期变更）。`ui_clock_overlay_apply` 按 flush area 窗口计算，天然支持小区域。收益：每秒 QSPI DMA 从 460KB 降到 ~5KB，render+overlay CPU burst 从 ~40ms 降到 ~2ms，SPI 驱动 PM 锁持有时间同步缩短
4. 低功耗状态暴露：main.c 新增 `app_state_low_power()` getter（ui_clock/日志判断用）

### 4.5 子系统睡眠兼容性盘点

| 子系统 | 结论 | 依据/措施 |
|--------|------|----------|
| QSPI LCD（esp_lcd sh8601 + DMA） | 理论安全 | CONFIG_PM 下 SPI 驱动在 DMA 事务期间持有 PM 锁；真机验证花屏 |
| I2C（RTC/触摸/AXP） | 安全 | 事务只发生在任务运行中（CPU 占用期间不会自动入睡）；i2c_master 总线锁 |
| LVGL | 安全（须先做 §4.3 tick 源修复） | lv_timer_handler 由主循环驱动；2ms tick 定时器必须删除，否则入睡永不满足条件 |
| WiFi | 兼容 + S2 联锁 | esp_wifi 与 esp_pm 官方兼容；对时进行中暂缓 S2（见 §4.1 联锁） |
| 系统时钟 | 安全 | tickless 下 esp_timer 保持；显示时间来自 PCF85063 外部 RTC 每秒 I2C 读 |
| USB-JTAG | 见 4.2 风险 | S0 态永不睡；C6 的 USJ light-sleep 保持 cap 未实现，睡即断连 |

**S2 入睡生效性的可观测手段**（builder check 提出的问题，自行裁定）：电池态没有 console，不能依赖 `esp_pm_dump_locks`。实现为：
1. `tracePRE_SLEEP_PROCESSING` 钩子累计入睡次数/最近入睡时刻（RAM 计数器，零开销）
2. 新 console 命令 `PMSTAT`：报告入睡计数、当前 pm 态；`PMFORCE S2`：无视 vbus 强制进 S2 一分钟（插线状态下快速验证锁行为与入睡是否发生）
3. 终极证据仍是放电斜率（§5.4）

### 4.6 文件改动清单

| 文件 | 改动 |
|------|------|
| `main/app_pm.c/h`（新） | esp_pm 状态机 + vbus/lowpower 输入 + WiFi 联锁 + 查询 + 睡眠计数器 |
| `main/main.c` | gui_task 接入（idle 状态机、1s vbus 轮询、循环 delay 分态）；time_sync_task 收字节切 S0；PMSTAT/PMFORCE 命令；app_state_low_power() |
| `main/bsp_lcd.c` | 删除 2ms lvgl tick 定时器（改 LV_TICK_CUSTOM） |
| `main/ui_clock.c` | 日志静默 + 秒位局部 invalidate |
| `sdkconfig.defaults` | PM_ENABLE + TICKLESS_IDLE + LV_TICK_CUSTOM（含 SYS_TIME_EXPR 指向 esp_timer） |
| `main/CMakeLists.txt` | 加 app_pm.c |
| `AGENTS.md` / `docs/features/` | 记录三态策略与 A/B 基准 |

## 5. 验证计划

1. **构建门槛**：`idf.py build` 零错误零警告（PM 相关 Kconfig 变更后 fullclean build）
2. **功能回归（S0 插线全速）**：走时/切页动画/亮度滑条/T9 键盘/WiFi 对时/SNAP 截图/BATT
3. **睡眠功能（S2 电池态）**：
   - `PMFORCE S2`（插线态强制入睡一分钟）+ `PMSTAT` 睡眠计数确认入睡真实发生
   - 触摸唤醒响应与亮度恢复正常；时钟整夜走时正确（BATTLOG lp 标志 + 次日 SNAP）
   - 拔线再插回：1s 内 console 恢复；枚举失败→重插即好（记录发生率）
4. **功耗 A/B**：满电拔线 ≥8h，85-95% 窗口斜率 <1.2%/h（基线 3.6%/h）
5. **最终验收**：满电放没电 ≥48h（顺带拿到真实尾段/截止电压，校准可用容量假设）

## 6. 风险与回退

| 风险 | 概率 | 缓解/回退 |
|------|------|----------|
| QSPI flush 期间入睡导致花屏 | 低 | SPI 驱动 PM 锁；若复现→flush 窗口自持 PM 锁 |
| USB 插回枚举失败 | 中 | 1s vbus 轮询+字节唤醒；仍高→200ms 轮询；文档标注"重插即好" |
| tickless 下触摸唤醒延迟 >50ms | 低 | indev 20Hz 采样；可回退循环 delay |
| 功耗不达标（落在 20-24mA） | 中 | 后续优化清单逐项补（见 §7） |
| 回退开关 | — | `app_pm` 整体禁用时退化为单态 {160,160,false}（等于现状） |

## 7. 后续优化空间（本期不做，按性价比排序）

1. ALDO1（麦克风轨）关闭实验——逐项 A/B 测斜率，可能白吃 1-3mA
2. CST9220 深度睡眠 + INT 中断驱动读（省 ~0.5-1mA，改动在 bsp_touch 轮询架构）
3. 点阵灰度 229→180（省面板 ~0.8mA，视觉差异需用户确认）
4. 屏幕完全熄灭只留走时（省 ~7mA，**需用户改变"最低可见"需求才做**）
5. 更大容量电池（硬件选项）
