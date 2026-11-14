import { useEffect, useState } from "react";
import type { ConfigPatch, DeviceConfig } from "../lib/device";

interface Props {
  cfg: DeviceConfig;
  onSave: (patch: ConfigPatch) => Promise<void>;
  /** 固件是否支持（proto>=4）。旧固件 SETCFG 对未知 key 静默忽略，门控防「保存成功」假象 */
  supported: boolean;
}

const MODES: Array<{ v: number; label: string }> = [
  { v: 0, label: "关闭" },
  { v: 1, label: "单次" },
  { v: 2, label: "每周" },
  { v: 3, label: "每天" },
];

/** 周一(bit0) … 周日(bit6)，与固件 alarm_weekdays 位序一致 */
const WEEKDAYS = ["一", "二", "三", "四", "五", "六", "日"];

/** 闹钟：模式（关闭/单次/每周/每天）+ 时间 + 周几多选；一次保存提交全部字段 */
export default function AlarmCard({ cfg, onSave, supported }: Props) {
  const [mode, setMode] = useState(cfg.alarm_mode ?? 0);
  const [hour, setHour] = useState(cfg.alarm_hour ?? 7);
  const [minute, setMinute] = useState(cfg.alarm_min ?? 0);
  const [weekdays, setWeekdays] = useState(cfg.alarm_weekdays ?? 127);
  const [saving, setSaving] = useState(false);
  const [saved, setSaved] = useState(false);
  const [error, setError] = useState("");

  useEffect(() => {
    setMode(cfg.alarm_mode ?? 0);
    setHour(cfg.alarm_hour ?? 7);
    setMinute(cfg.alarm_min ?? 0);
    setWeekdays(cfg.alarm_weekdays ?? 127);
  }, [cfg]);

  const devWeekdays = cfg.alarm_weekdays ?? 127;
  const dirty =
    mode !== (cfg.alarm_mode ?? 0) ||
    hour !== (cfg.alarm_hour ?? 7) ||
    minute !== (cfg.alarm_min ?? 0) ||
    weekdays !== devWeekdays;
  const weekdaysEmpty = mode === 2 && weekdays === 0;
  const canSave = supported && !saving && dirty && !weekdaysEmpty;

  const toggleWeekday = (bit: number) => {
    setWeekdays((w) => w ^ (1 << bit));
    setSaved(false);
  };

  const save = async () => {
    setSaving(true);
    setError("");
    try {
      await onSave({ alarm_mode: mode, alarm_hour: hour, alarm_min: minute, alarm_weekdays: weekdays });
      setSaved(true);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setSaving(false);
    }
  };

  const p2 = (n: number) => String(n).padStart(2, "0");

  return (
    <section className="card">
      <div className="card-head">
        <h2>闹钟</h2>
        {saved && !dirty && <span className="saved-tag">已保存 ✓</span>}
      </div>
      <p className="desc">
        {supported
          ? "到点响铃 3 分钟（声音渐入至最大音量），按设备 KEY 键停止响铃；单次闹钟触发后自动关闭"
          : "当前固件不支持此功能：需固件协议 proto 4 及以上（升级固件后可用）"}
      </p>
      <div className="alarm-grid">
        <div className="field">
          <label>模式</label>
          <select
            value={mode}
            disabled={!supported}
            onChange={(e) => {
              setMode(Number(e.target.value));
              setSaved(false);
            }}
          >
            {MODES.map((m) => (
              <option key={m.v} value={m.v}>
                {m.label}
              </option>
            ))}
          </select>
        </div>
        <div className="field">
          <label>时间</label>
          <div className="alarm-time">
            <select
              value={hour}
              disabled={!supported || mode === 0}
              onChange={(e) => {
                setHour(Number(e.target.value));
                setSaved(false);
              }}
            >
              {Array.from({ length: 24 }, (_, h) => (
                <option key={h} value={h}>
                  {p2(h)}
                </option>
              ))}
            </select>
            <span className="alarm-colon">:</span>
            <select
              value={minute}
              disabled={!supported || mode === 0}
              onChange={(e) => {
                setMinute(Number(e.target.value));
                setSaved(false);
              }}
            >
              {Array.from({ length: 60 }, (_, m) => (
                <option key={m} value={m}>
                  {p2(m)}
                </option>
              ))}
            </select>
          </div>
        </div>
      </div>
      {mode === 2 && (
        <div className="field">
          <label>重复的星期（至少选一天）</label>
          <div className="weekday-chips">
            {WEEKDAYS.map((label, bit) => (
              <button
                key={bit}
                type="button"
                className={`wd-chip${(weekdays & (1 << bit)) !== 0 ? " on" : ""}`}
                disabled={!supported}
                onClick={() => toggleWeekday(bit)}
              >
                {label}
              </button>
            ))}
          </div>
        </div>
      )}
      <div className="card-actions">
        <button className="btn primary" disabled={!canSave} onClick={() => void save()}>
          {saving ? "保存中…" : "保存"}
        </button>
      </div>
      {weekdaysEmpty && <p className="card-error">每周模式需至少选择一个星期</p>}
      {error && <p className="card-error">{error}</p>}
    </section>
  );
}
