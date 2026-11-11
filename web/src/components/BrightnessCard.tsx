import { useEffect, useState } from "react";

interface Props {
  brightness: number;
  onChange: (value: number) => Promise<void>;
}

/** 屏幕亮度：拖动本地即时反馈，松手（pointerup/键盘）才提交设备 */
export default function BrightnessCard({ brightness, onChange }: Props) {
  const [value, setValue] = useState(brightness);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState("");

  useEffect(() => setValue(brightness), [brightness]);

  const commit = async () => {
    if (value === brightness || pending) return;
    setPending(true);
    setError("");
    try {
      await onChange(value);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
      setValue(brightness); // 失败回滚到设备实际值
    } finally {
      setPending(false);
    }
  };

  return (
    <section className="card">
      <div className="card-head">
        <h2>屏幕亮度</h2>
        {pending && <span className="badge">同步中…</span>}
      </div>
      <p className="desc">调整后立即生效并记住，下次开机保持</p>
      <div className="bright-row">
        <input
          type="range"
          min={0}
          max={100}
          step={1}
          value={value}
          disabled={pending}
          onChange={(e) => setValue(Number(e.target.value))}
          onPointerUp={() => void commit()}
          onKeyUp={() => void commit()}
        />
        <span className="bright-value">{value}%</span>
      </div>
      {error && (
        <div className="card-actions">
          <p className="card-error">{error}</p>
        </div>
      )}
    </section>
  );
}
