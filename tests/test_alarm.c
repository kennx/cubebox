/*
 * alarm 主机端测试：触发判定（模式/星期位/分钟去重/跨天复响）与渐入包络。
 * 直接编译 main/alarm.c（纯 C，无 IDF/LVGL 依赖）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "alarm.h"

static void expect(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", label);
        exit(1);
    }
}

/* 构造本地时间：2026-11-14 是周六（tm_wday=6） */
static struct tm make_tm(int hour, int min, int wday)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = 126; /* 2026 */
    t.tm_mon = 10;   /* 11 月 */
    t.tm_mday = 14 + (wday - 6 < 0 ? wday + 1 : wday - 6); /* 仅占位，判定不用日期 */
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_wday = wday;
    return t;
}

int main(void)
{
    alarm_cfg_t cfg;
    alarm_sched_ctx_t ctx;

    /* ---- 默认关：到点不响 ---- */
    cfg.mode = ALARM_MODE_OFF;
    cfg.hour = 7;
    cfg.minute = 0;
    cfg.weekdays = 127;
    alarm_sched_reset(&ctx);
    expect(ctx.last_fired_key < 0, "reset clears fired key");
    {
        struct tm t = make_tm(7, 0, 6);
        expect(alarm_sched_check(&cfg, &t, 1763084400, &ctx) == false, "mode off never fires");
    }

    /* ---- 每天：到点响，同分钟去重，过分钟不响，次日同点再响 ---- */
    cfg.mode = ALARM_MODE_DAILY;
    {
        time_t t0 = 1763084400; /* 任意合法 epoch（仅作分钟去重键，与 tm 的 HH:MM/wday 无关联） */
        struct tm t = make_tm(7, 0, 6);
        expect(alarm_sched_check(&cfg, &t, t0, &ctx) == true, "daily fires at HH:MM");
        expect(alarm_sched_check(&cfg, &t, t0 + 1, &ctx) == false, "same minute deduped");
        expect(alarm_sched_check(&cfg, &t, t0 + 59, &ctx) == false, "same minute late second deduped");
        struct tm t2 = make_tm(7, 1, 6);
        expect(alarm_sched_check(&cfg, &t2, t0 + 60, &ctx) == false, "next minute no fire");
        struct tm t3 = make_tm(7, 0, 0); /* 次日（周日） */
        expect(alarm_sched_check(&cfg, &t3, t0 + 86400, &ctx) == true, "next day fires again");
    }

    /* ---- 每周：星期位换算 tm_wday(0=周日) → bit(周一=bit0…周日=bit6) ---- */
    cfg.mode = ALARM_MODE_WEEKLY;
    cfg.hour = 7;
    cfg.minute = 30;
    alarm_sched_reset(&ctx);
    {
        /* 周日 tm_wday=0 → bit6（0x40） */
        cfg.weekdays = 0x40;
        struct tm sun = make_tm(7, 30, 0);
        expect(alarm_sched_check(&cfg, &sun, 1763087400, &ctx) == true, "weekly sunday bit6 fires");
        /* 周一 tm_wday=1 → bit0 */
        alarm_sched_reset(&ctx);
        cfg.weekdays = 0x01;
        struct tm mon = make_tm(7, 30, 1);
        expect(alarm_sched_check(&cfg, &mon, 1763173800, &ctx) == true, "weekly monday bit0 fires");
        /* 周六 tm_wday=6 → bit5 */
        alarm_sched_reset(&ctx);
        cfg.weekdays = 0x20;
        struct tm sat = make_tm(7, 30, 6);
        expect(alarm_sched_check(&cfg, &sat, 1763087400 - 86400, &ctx) == true, "weekly saturday bit5 fires");
        /* 未选中的天不响：weekdays 只有周一，今天是周六 */
        alarm_sched_reset(&ctx);
        cfg.weekdays = 0x01;
        expect(alarm_sched_check(&cfg, &sat, 1763087400, &ctx) == false, "weekly unselected day no fire");
        /* weekdays=0：任何天都不响（合法但无效的配置） */
        alarm_sched_reset(&ctx);
        cfg.weekdays = 0;
        expect(alarm_sched_check(&cfg, &sat, 1763087400, &ctx) == false, "weekly zero weekdays no fire");
        /* 全选 127：周六响 */
        alarm_sched_reset(&ctx);
        cfg.weekdays = 127;
        expect(alarm_sched_check(&cfg, &sat, 1763087400, &ctx) == true, "weekly all weekdays fires");
    }

    /* ---- 单次：到点即响（关闹钟的持久化由调用方负责） ---- */
    cfg.mode = ALARM_MODE_ONCE;
    cfg.hour = 23;
    cfg.minute = 59;
    cfg.weekdays = 0;
    alarm_sched_reset(&ctx);
    {
        struct tm t = make_tm(23, 59, 3);
        expect(alarm_sched_check(&cfg, &t, 1763140740, &ctx) == true, "once fires ignoring weekdays");
        struct tm t2 = make_tm(23, 59, 3);
        expect(alarm_sched_check(&cfg, &t2, 1763140740 + 30, &ctx) == false, "once same minute deduped");
    }

    /* ---- 时间不匹配：分/时任一不同都不响 ---- */
    cfg.mode = ALARM_MODE_DAILY;
    cfg.hour = 9;
    cfg.minute = 15;
    alarm_sched_reset(&ctx);
    {
        struct tm hm = make_tm(9, 16, 2);
        expect(alarm_sched_check(&cfg, &hm, 1763002560, &ctx) == false, "minute mismatch no fire");
        struct tm hh = make_tm(8, 15, 2);
        expect(alarm_sched_check(&cfg, &hh, 1763001960, &ctx) == false, "hour mismatch no fire");
    }

    /* ---- 非法模式值（防御：越界 NVS 值不得乱响） ---- */
    cfg.mode = 4;
    alarm_sched_reset(&ctx);
    {
        struct tm t = make_tm(9, 15, 2);
        expect(alarm_sched_check(&cfg, &t, 1763002560 - 60, &ctx) == false, "invalid mode treated as off");
    }

    /* ---- 渐入包络：Q8 定点，起步 13（≈5%），30s 线性到 256，之后钳制 ---- */
    expect(alarm_fade_gain_q8(0) == 13, "fade starts at 13/256");
    expect(alarm_fade_gain_q8(1) == 13, "fade 1ms still near start");
    expect(alarm_fade_gain_q8(15000) == 134, "fade half way 134/256");
    expect(alarm_fade_gain_q8(29999) == 255, "fade just before full");
    expect(alarm_fade_gain_q8(30000) == 256, "fade full at 30s");
    expect(alarm_fade_gain_q8(60000) == 256, "fade clamped after 30s");
    {
        /* 单调不减 */
        uint32_t prev = 0;
        for (uint32_t ms = 0; ms <= 30000; ms += 250) {
            uint32_t g = alarm_fade_gain_q8(ms);
            expect(g >= prev, "fade monotonic");
            prev = g;
        }
    }

    /* ---- 常量钉死（用户需求：响 3 分钟） ---- */
    expect(ALARM_RING_MS == 180000, "ring duration 3 minutes");

    printf("test_alarm: all passed\n");
    return 0;
}
