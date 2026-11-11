import { useEffect, useState } from "react";
import type { Device, DeviceConfig, WifiNetwork } from "../lib/device";

interface Props {
  device: Device;
  cfg: DeviceConfig;
  onSave: (ssid: string, pass: string) => Promise<void>;
}

/* RSSI 四档信号条（约 -60/-70/-80 dBm 分档） */
function SignalBars({ rssi }: { rssi: number }) {
  const level = rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1;
  return (
    <span className="sig" title={`${rssi} dBm`}>
      {["▂", "▄", "▆", "█"].map((b, i) => (
        <span key={i} className={i < level ? "on" : "off"}>
          {b}
        </span>
      ))}
    </span>
  );
}

/** Wi-Fi 凭据：SSID 明文 + 附近网络扫描选择；密码留空 = 保持已存密码不变（设备不回传明文） */
export default function WifiCard({ device, cfg, onSave }: Props) {
  const [ssid, setSsid] = useState(cfg.wifi_ssid);
  const [pass, setPass] = useState("");
  const [showPass, setShowPass] = useState(false);
  const [saving, setSaving] = useState(false);
  const [saved, setSaved] = useState(false);
  const [error, setError] = useState("");
  const [scanning, setScanning] = useState(false);
  const [networks, setNetworks] = useState<WifiNetwork[] | null>(null);
  const [scanError, setScanError] = useState("");

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

  /* 设备阻塞扫描 ~2-4s（请求已串行化，不影响其他操作） */
  const scan = async () => {
    setScanning(true);
    setScanError("");
    try {
      setNetworks(await device.scan());
    } catch (e) {
      setScanError(e instanceof Error ? e.message : String(e));
    } finally {
      setScanning(false);
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
      <p className="desc">保存后设备自动连接，用于 NTP 校时等联网功能</p>
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
        <button type="button" className="btn ghost" disabled={scanning} onClick={scan}>
          {scanning ? "扫描中…" : "扫描附近 Wi-Fi"}
        </button>
        {saved && !dirty && <span className="saved-tag">已保存 ✓</span>}
        {scanError && <p className="card-error">{scanError}</p>}
      </div>
      {networks && (
        <div className="scan-list">
          {networks.length === 0 ? (
            <p className="muted scan-empty">附近未发现网络</p>
          ) : (
            networks.map((n) => (
              <button
                type="button"
                key={n.ssid}
                className="scan-item"
                onClick={() => {
                  setSsid(n.ssid);
                  setSaved(false);
                }}
                title="点击填入 SSID"
              >
                <span className="scan-ssid">
                  {n.ssid}
                  {!n.secure && <span className="scan-open">开放</span>}
                </span>
                <span className="scan-meta">
                  {n.secure && <span className="scan-lock" title="加密网络">🔒</span>}
                  <SignalBars rssi={n.rssi} />
                </span>
              </button>
            ))
          )}
        </div>
      )}
      {error && <p className="card-error">{error}</p>}
    </section>
  );
}
