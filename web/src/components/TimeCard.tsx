import { useEffect, useState } from "react";
import type { Device, DeviceConfig, TimeInfo } from "../lib/device";

interface Props {
  device: Device;
  cfg: DeviceConfig;
  onSave: (ntpServer: string) => Promise<void>;
}

/** 时钟页：NTP 服务器配置 + 设备当前时间与同步状态（每 10s 轮询 TIME） */
export default function TimeCard({ device, cfg, onSave }: Props) {
  const [ntp, setNtp] = useState(cfg.ntp_server);
  const [time, setTime] = useState<TimeInfo | null>(null);
  const [saving, setSaving] = useState(false);
  const [saved, setSaved] = useState(false);
  const [error, setError] = useState("");
  const [timeError, setTimeError] = useState("");

  useEffect(() => setNtp(cfg.ntp_server), [cfg.ntp_server]);

  /* 挂载即查询 + 每 10s 刷新；卸载/重连时停止 */
  useEffect(() => {
    let alive = true;
    const refresh = () =>
      device
        .getTime()
        .then((t) => {
          if (alive) {
            setTime(t);
            setTimeError("");
          }
        })
        .catch((e) => alive && setTimeError(e instanceof Error ? e.message : String(e)));
    refresh();
    const timer = window.setInterval(refresh, 10000);
    return () => {
      alive = false;
      window.clearInterval(timer);
    };
  }, [device]);

  const dirty = ntp !== cfg.ntp_server;
  const canSave = !saving && dirty && ntp.length > 0;

  const save = async () => {
    setSaving(true);
    setError("");
    try {
      await onSave(ntp.trim());
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
        <h2>时间同步</h2>
        {time && (
          <span className={`badge${time.synced ? " ok" : ""}`}>
            {time.synced ? "已同步 · NTP" : "未同步"}
          </span>
        )}
      </div>
      <p className="desc">
        设备连上 Wi-Fi 后自动经 NTP 服务器校时并写入 RTC（掉电走时）；时区在「通用配置」中设置
      </p>
      <div className="device-time" title={time ? `UTC ${time.utc}` : undefined}>
        {time ? time.local : "…"}
      </div>
      {!time && timeError && <p className="card-error">{timeError}</p>}
      {time && !time.synced && !timeError && (
        <p className="card-error">尚未同步：请先在「通用配置」中配置并保存 Wi-Fi</p>
      )}
      <div className="card-row">
        <div className="field">
          <label htmlFor="ntp-server">NTP 服务器（域名 / IP）</label>
          <input
            id="ntp-server"
            type="text"
            value={ntp}
            maxLength={63}
            placeholder="例如 ntp.aliyun.com"
            onChange={(e) => {
              setNtp(e.target.value);
              setSaved(false);
            }}
          />
        </div>
        <button type="button" className="btn primary" disabled={!canSave} onClick={save}>
          {saving ? "保存中…" : "保存"}
        </button>
      </div>
      <div className="card-actions">
        {saved && !dirty && <span className="saved-tag">已保存 ✓</span>}
        {error && <p className="card-error">{error}</p>}
      </div>
    </section>
  );
}
