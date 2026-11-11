import { useState } from "react";

interface Props {
  tz: string;
  onSave: (tz: string) => Promise<void>;
}

const TZ_OPTIONS: Array<{ v: string; label: string }> = [
  { v: "CST-8", label: "(UTC+08:00) 北京" },
  { v: "HKT-8", label: "(UTC+08:00) 香港" },
  { v: "JST-9", label: "(UTC+09:00) 东京" },
  { v: "WIB-7", label: "(UTC+07:00) 曼谷" },
  { v: "IST-5:30", label: "(UTC+05:30) 新德里" },
  { v: "MSK-3", label: "(UTC+03:00) 莫斯科" },
  { v: "EET-2EET,M3.5.0/3,M10.5.0/4", label: "(UTC+02:00) 东欧（含夏令时）" },
  { v: "CET-1CEST,M3.5.0,M10.5.0/3", label: "(UTC+01:00) 中欧（含夏令时）" },
  { v: "GMT0BST,M3.5.0/1,M10.5.0", label: "(UTC+00:00) 伦敦（含夏令时）" },
  { v: "AEST-10AEDT,M10.1.0,M4.1.0/3", label: "(UTC+10:00) 悉尼（含夏令时）" },
  { v: "NZST-12NZDT,M9.5.0,M4.1.0/3", label: "(UTC+12:00) 奥克兰（含夏令时）" },
  { v: "EST5EDT,M3.2.0,M11.1.0", label: "(UTC−05:00) 纽约（含夏令时）" },
  { v: "PST8PDT,M3.2.0,M11.1.0", label: "(UTC−08:00) 洛杉矶（含夏令时）" },
  { v: "UTC0", label: "UTC 协调世界时" },
];

const CUSTOM = "__custom__";

/** 时区：常用下拉（含夏令时规则）+ 自定义 POSIX TZ 输入 */
export default function TimezoneCard({ tz, onSave }: Props) {
  const known = TZ_OPTIONS.some((o) => o.v === tz);
  const [sel, setSel] = useState(known ? tz : CUSTOM);
  const [custom, setCustom] = useState(known ? "" : tz);
  const [saving, setSaving] = useState(false);
  const [saved, setSaved] = useState(false);
  const [error, setError] = useState("");

  const value = sel === CUSTOM ? custom.trim() : sel;
  const dirty = value !== tz;
  const canSave = !saving && dirty && value.length > 0;

  const save = async () => {
    setSaving(true);
    setError("");
    try {
      await onSave(value);
      setSaved(true);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setSaving(false);
    }
  };

  return (
    <section className="card">
      <div className="card-head">
        <h2>时区</h2>
        {saved && !dirty && <span className="saved-tag">已保存 ✓</span>}
      </div>
      <p className="desc">保存后设备时钟立即按新时区显示</p>
      <div className="card-row">
        <div className="field">
          <label htmlFor="tz-select">常用时区</label>
          <select
            id="tz-select"
            value={sel}
            onChange={(e) => {
              setSel(e.target.value);
              setSaved(false);
            }}
          >
            {TZ_OPTIONS.map((o) => (
              <option key={o.v} value={o.v}>
                {o.label}
              </option>
            ))}
            <option value={CUSTOM}>自定义…</option>
          </select>
        </div>
        {sel === CUSTOM && (
          <div className="field">
            <label htmlFor="tz-custom">POSIX TZ 串</label>
            <input
              id="tz-custom"
              type="text"
              value={custom}
              maxLength={39}
              placeholder="例如 JST-9"
              onChange={(e) => {
                setCustom(e.target.value);
                setSaved(false);
              }}
            />
          </div>
        )}
        <button type="button" className="btn primary" disabled={!canSave} onClick={save}>
          {saving ? "保存中…" : "保存"}
        </button>
      </div>
      <div className="card-actions">
        {error && <p className="card-error">{error}</p>}
      </div>
    </section>
  );
}
