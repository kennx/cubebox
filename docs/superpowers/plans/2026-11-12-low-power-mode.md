# 低功耗模式（自动降亮度回时钟）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 无人交互超时（默认 30s，可选 1/5/10 分钟、永不）自动降亮度至 20% 并切回时钟 app，触摸或 KEY(GPIO10) 唤醒；含网页配置项。

**Architecture:** gui_task 10ms 轮询驱动纯逻辑状态机（`main/low_power.c`，主机可测）；KEY 经新 HAL `hal_button`（GPIO10 内部上拉、低有效）；超时值走既有 config_model/config_store 链路（新字段 `idle_timeout`）；web 通用页加选择卡片。唤醒动作在 gui_task 薄胶水层执行（`apps_request_open` + 背光）。

**Tech Stack:** ESP-IDF v5.5 (C) / LVGL v8 / React+TS (Vite) / 主机端 CMake+ctest。

**Spec:** `docs/superpowers/specs/2026-11-12-low-power-design.md`

## Global Constraints

- 白名单值（秒）：{0, 30, 60, 300, 600}；0=永不；默认 30（用户原始「用不」= 永不）。
- dim 亮度 = min(20, 配置亮度)；唤醒恢复为配置亮度（实时读取）。
- 所有 LVGL 调用仅限 gui_task；跨任务切 app 仅经 `apps_request_open()`。
- KEY=GPIO10（原理图 Key3，无外部上拉，按下接地→低有效，内部上拉）。
- 协议版本升 proto 3（先例：proto 2=加字段+加命令）。
- 主机可测模块改后必须跑 `tests/`；`idf.py build` 零新增警告；`web` 需 `npm run build` 通过。
- 新增 main/ 根源文件须显式加入 `main/CMakeLists.txt` SRCS（hal/ 是 GLOB，需 `idf.py reconfigure`）。

## Review Focus

1. `SETCFG {"idle_timeout":45}` 非白名单 → 整体拒绝（config_model 测试覆盖）。
2. `SETCFG {"idle_timeout":"30"}` 类型不匹配 → 拒绝（config_model 测试覆盖）。
3. 低功耗期间网页改亮度 → apply_pending 会设全亮，须压回 dim（apply_pending 返回 bool，胶水重设；代码审查关注）。
4. 唤醒触摸透传 LVGL（可兼作滑动）不破坏状态：EXIT 只触发一次（low_power 测试覆盖）。
5. 永不(0) 期间不进入；dimmed 时改为 0 → 退出（low_power 测试覆盖）。
6. 旧固件 #CFG 无 `idle_timeout` → web 卡片回退 30 不崩（web 代码 `?? 30`）。

---

### Task 1: config_model 增加 idle_timeout（TDD）

**Files:**
- Modify: `tests/test_config_model.c`（defaults / set_number / 类型不匹配 / to_json 各块插入用例）
- Modify: `main/config_model.h`（struct 字段 + 注释）
- Modify: `main/config_model.c`（defaults / key_type / set_number / set_string 拒绝 / to_json）

**Interfaces:**
- Produces: `config_model_t` 新增 `uint16_t idle_timeout`；JSON 键 `"idle_timeout"`（to_json 置于 `"brightness"` 之后）；默认 30；白名单 {0,30,60,300,600}；已知 key 传字符串 → `config_model_set_string` 返回 false。

- [ ] **Step 1: 写失败测试** —— defaults 块加 `expect(cfg.idle_timeout == 30, "default idle_timeout 30")`；set_number 块加：0/30/60/300/600 接受并存储，45/29/31/6000/-1/30.5 拒绝且值不变；类型块加 `set_number(&cfg,"idle_timeout",...)` 走 number、`set_string(&cfg,"idle_timeout","30") == false`；to_json 精确串改为含 `"idle_timeout":30`（defaults 与 pass_set 两处）。
- [ ] **Step 2: 跑测试确认失败** —— `cmake --build tests/build && ctest --test-dir tests/build --output-on-failure` → 编译失败（无该字段）。
- [ ] **Step 3: 实现** —— `idle_timeout` 白名单判断用 switch 或数组成员循环；`config_model_set_number` 新分支（整数校验同 brightness）；`config_model_key_type` 加 NUMBER；`config_model_set_string` 加该 key 拒绝分支；`to_json` 在 brightness 后追加 `,"idle_timeout":<u16>`。
- [ ] **Step 4: 跑测试确认通过**
- [ ] **Step 5: Commit** —— `feat: config_model 新增 idle_timeout 低功耗超时字段（白名单 0/30/60/300/600 秒）`

### Task 2: low_power 纯状态机（TDD）

**Files:**
- Create: `main/low_power.h`, `main/low_power.c`
- Create: `tests/test_low_power.c`
- Modify: `tests/CMakeLists.txt`（新 target，编 low_power.c）

**Interfaces:**
- Produces:
  - `typedef enum { LOW_POWER_NONE, LOW_POWER_ENTER, LOW_POWER_EXIT } low_power_action_t;`
  - `typedef struct { bool dimmed; uint32_t prev_idle_ms; bool prev_key; } low_power_ctx_t;`
  - `void low_power_reset(low_power_ctx_t *ctx);`
  - `low_power_action_t low_power_step(low_power_ctx_t *ctx, uint32_t idle_ms, bool key_pressed, uint32_t timeout_s);`
  - 语义：触摸活动 = `idle_ms < ctx->prev_idle_ms`；KEY 下降沿 = `key_pressed && !prev_key`；AWAKE 且 `timeout_s!=0 && idle_ms >= timeout_s*1000` → ENTER；DIMMED 且（活动或 KEY 沿）→ EXIT；DIMMED 且 `timeout_s==0` → EXIT；EXIT 仅在转变那一轮返回一次。

- [ ] **Step 1: 写失败测试** —— 用例：初态不动作；idle 29999ms 不进 / 30000ms 进（timeout 30）；ENTER 后持续 idle 增长保持 NONE；dimmed 时 idle 回落 → EXIT；dimmed 时 KEY 下降沿 → EXIT；KEY 持续按住（沿已过）不再 EXIT；清醒期 KEY 沿不单独推迟超时（推迟由胶水层 trig_activity 注入 idle 回落；纯层 idle 到点即 ENTER，见 test_low_power.c 注释）；timeout=0 永不进；dimmed 时 timeout 变 0 → EXIT；EXIT 后下一轮 NONE（只触发一次）。
- [ ] **Step 2: 跑测试确认失败**（编译错误：无头文件）
- [ ] **Step 3: 实现**（纯 C，无 IDF/LVGL 依赖）
- [ ] **Step 4: 跑测试确认通过**
- [ ] **Step 5: Commit** —— `feat: low_power 纯状态机（超时进入/触摸或按键退出，主机可测）`

### Task 3: config_store 持久化 + getter

**Files:**
- Modify: `main/config_store.c`, `main/config_store.h`

**Interfaces:**
- Consumes: Task 1 的 `idle_timeout` 字段。
- Produces: `uint16_t config_store_get_idle_timeout(void);`、`uint8_t config_store_get_brightness(void);`；`bool config_store_apply_pending(void)`（返回本轮是否真的应用了 pending——胶水据此在低功耗期压回 dim）；NVS key `"idle_sec"`（u16，init 读经 `config_model_set_number` 验证，set_json 写）。

- [ ] **Step 1: 实现**（NVS 读写对齐现有 bright 模式；主机无测试——IDF 依赖，验证靠 Task 4 的 idf.py build + Task 7 真机 SETCFG/GETCFG 回环）
- [ ] **Step 2: Commit** —— `feat: config_store 持久化 idle_timeout 并暴露运行时 getter`

### Task 4: hal_button + main.c 接入（含 WAKE 命令、proto 3）

**Files:**
- Create: `main/hal/hal_button.h`, `main/hal/hal_button.c`
- Modify: `main/hal/hal.h`（聚合）
- Modify: `main/main.c`（app_main 调 `hal_button_init()`；gui_task 胶水；console `WAKE`；PING 回 `proto":3`；注释更新）
- Modify: `main/CMakeLists.txt`（SRCS 加 `low_power.c`；hal_button.c 由 GLOB 覆盖）

**Interfaces:**
- Consumes: Task 1/2/3 全部接口；`lv_disp_get_inactive_time(NULL)`、`lv_disp_trig_activity(NULL)`、`apps_request_open`、`hal_display_set_backlight`。
- Produces: `void hal_button_init(void);`（GPIO10 输入+内部上拉）、`bool hal_button_pressed(void);`（低电平=按下）；gui_task 每轮（apply_pending 之后、apps_update 之前）跑 low_power_step：ENTER→`apps_request_open(app_clock.name)` + 背光 min(20,亮度) + `ESP_LOGI`；EXIT→恢复配置亮度 + log；`apply_pending()` 返回 true 且仍 dimmed → 重设 dim；KEY 下降沿 → `lv_disp_trig_activity(NULL)`（清醒期 KEY 也推迟超时）；`WAKE` 命令置 volatile 标志由 gui_task 消费（注入同 EXIT 路径）；`#HELLO` proto 3。

- [ ] **Step 1: 实现 hal_button**（`driver/gpio.h`，无外部依赖句柄，失败 ESP_LOGE 不致命）
- [ ] **Step 2: 实现 main.c 胶水与 WAKE**
- [ ] **Step 3: `idf.py reconfigure && idf.py build` 零新增警告**
- [ ] **Step 4: Commit** —— `feat: 低功耗模式接入 gui_task——超时降亮度回时钟，触摸/KEY/WAKE 唤醒`

### Task 5: web 配置卡片

**Files:**
- Create: `web/src/components/IdleTimeoutCard.tsx`
- Modify: `web/src/lib/device.ts`（DeviceConfig/ConfigPatch/MockState/MOCK_DEFAULTS/mockValidate/getCfg/setCfg）
- Modify: `web/src/components/ConfigScreen.tsx`（saveIdleTimeout + 卡片 + 副标题文案）

**Interfaces:**
- Consumes: 固件 `#CFG` JSON 新键 `"idle_timeout"`（number）。
- Produces: `<IdleTimeoutCard idleTimeout={cfg.idle_timeout ?? 30} onChange={saveIdleTimeout} />`；选项 30秒/1分钟/5分钟/10分钟/永不 → 30/60/300/600/0；mockValidate 白名单与固件一致；选择即提交（骨架对照 BrightnessCard：pending/error/失败回滚）。

- [ ] **Step 1: 实现**（TS 严格模式）
- [ ] **Step 2: `cd web && npm run build` 通过**
- [ ] **Step 3: Commit** —— `feat(web): 通用配置新增自动低功耗选择卡片`

### Task 6: 文档同步

**Files:**
- Modify: `docs/features/web-config.md`（proto 3：WAKE 命令、idle_timeout 字段约束、#CFG 示例、proto 演进说明）
- Modify: `docs/hardwares/ESP32-C6-Touch-AMOLED-2.16.md`（按键表 KEY=GPIO10，标注来源=官方原理图解析）
- Modify: `AGENTS.md`（项目地图 low_power.*、console 命令表 WAKE、按键盘 KEY=GPIO10）
- Modify: `docs/superpowers/specs/2026-11-12-low-power-design.md`（hal_button 接口名对齐 `hal_button_pressed()`）

- [ ] **Step 1: 更新四份文档**
- [ ] **Step 2: Commit** —— `docs: 低功耗模式规格/硬件 KEY 引脚/项目地图同步`

### Task 7: 真机验证

- [ ] `tools/flash.sh` 烧录；`GETCFG` 含 `"idle_timeout":30`；`SETCFG {"idle_timeout":60}` 回环 + 重启保持
- [ ] `NEXT` 切到 test app → 等 30s → 日志 `low power: enter` + `SNAP` 截图显示时钟 app
- [ ] `WAKE` → 日志 `low power: wake`、亮度恢复；再等 60s 验证新超时
- [ ] `SETCFG {"idle_timeout":0}` → 不再进入；实物 KEY/触摸唤醒请用户顺手确认
- [ ] web `npm run dev`（mock）确认卡片交互；有条件连真机验证

### Task 8: code review + 收尾

- [ ] superpowers:requesting-code-review 派发评审，处理反馈（receiving-code-review 原则）
- [ ] 全量回归：tests/ctest + idf.py build + npm build
- [ ] 推送 `feature/low-power-mode` 到 origin
