import { useEffect, useState } from "react";

interface Props {
  idleTimeout: number; // 秒 {0,30,60,300,600}，0=永不
  onChange: (value: number) => Promise<void>;
}

const OPTIONS: Array<{ v: number; label: string }> = [
  { v: 30, label: "30 秒" },
  { v: 60, label: "1 分钟" },
  { v: 300, label: "5 分钟" },
  { v: 600, label: "10 分钟" },
  { v: 0, label: "永不" },
];

/** 自动低功耗：无操作超时后降亮度回时钟，触摸或 KEY 唤醒；选择即提交 */
export default function IdleTimeoutCard({ idleTimeout, onChange }: Props) {
  const [value, setValue] = useState(idleTimeout);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState("");

  useEffect(() => setValue(idleTimeout), [idleTimeout]);

  const commit = async (next: number) => {
    if (next === idleTimeout || pending) return;
    setPending(true);
    setError("");
    try {
      await onChange(next);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
      setValue(idleTimeout); // 失败回滚到设备实际值
    } finally {
      setPending(false);
    }
  };

  return (
    <section className="card">
      <div className="card-head">
        <h2>自动低功耗</h2>
        {pending && <span className="badge">同步中…</span>}
      </div>
      <p className="desc">无操作一段时间后自动降低亮度并回到时钟，触摸屏幕或按 KEY 键唤醒</p>
      <div className="card-actions">
        <select
          value={OPTIONS.some((o) => o.v === value) ? String(value) : ""}
          disabled={pending}
          onChange={(e) => {
            const next = Number(e.target.value);
            setValue(next);
            void commit(next);
          }}
        >
          {OPTIONS.some((o) => o.v === value) ? null : (
            <option value="">{value} 秒（未知值）</option>
          )}
          {OPTIONS.map((o) => (
            <option key={o.v} value={o.v}>
              {o.label}
            </option>
          ))}
        </select>
      </div>
      {error && <p className="card-error">{error}</p>}
    </section>
  );
}
