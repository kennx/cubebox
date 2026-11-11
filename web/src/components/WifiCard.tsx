import { useEffect, useState } from "react";
import type { DeviceConfig } from "../lib/device";

interface Props {
  cfg: DeviceConfig;
  onSave: (ssid: string, pass: string) => Promise<void>;
}

/** Wi-Fi 凭据：SSID 明文，密码留空 = 保持已存密码不变（设备不回传明文） */
export default function WifiCard({ cfg, onSave }: Props) {
  const [ssid, setSsid] = useState(cfg.wifi_ssid);
  const [pass, setPass] = useState("");
  const [showPass, setShowPass] = useState(false);
  const [saving, setSaving] = useState(false);
  const [saved, setSaved] = useState(false);
  const [error, setError] = useState("");

  useEffect(() => setSsid(cfg.wifi_ssid), [cfg.wifi_ssid]);

  const dirty = ssid !== cfg.wifi_ssid || pass !== "";
  const canSave = !saving && dirty && ssid.length > 0;

  const save = async () => {
    setSaving(true);
    setError("");
    try {
      await onSave(ssid, pass);
      setPass("");
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
        <h2>Wi-Fi</h2>
        {cfg.wifi_ssid ? (
          <span className="badge">已保存：{cfg.wifi_ssid}</span>
        ) : (
          <span className="badge">未配置</span>
        )}
      </div>
      <p className="desc">凭据保存在设备上，供后续 Wi-Fi 连网功能使用</p>
      <div className="card-row">
        <div className="field">
          <label htmlFor="wifi-ssid">网络名称（SSID）</label>
          <input
            id="wifi-ssid"
            type="text"
            value={ssid}
            maxLength={32}
            placeholder="例如 Home_5G"
            onChange={(e) => {
              setSsid(e.target.value);
              setSaved(false);
            }}
          />
        </div>
        <div className="field">
          <label htmlFor="wifi-pass">密码</label>
          <input
            id="wifi-pass"
            type={showPass ? "text" : "password"}
            value={pass}
            maxLength={64}
            placeholder={cfg.wifi_pass_set ? "已保存，留空保持不变" : "Wi-Fi 密码"}
            onChange={(e) => {
              setPass(e.target.value);
              setSaved(false);
            }}
          />
        </div>
        <button
          type="button"
          className="btn ghost"
          onClick={() => setShowPass((v) => !v)}
        >
          {showPass ? "隐藏" : "显示"}
        </button>
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
