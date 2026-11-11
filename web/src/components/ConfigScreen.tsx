import { useEffect, useState } from "react";
import type { Device, DeviceConfig, DeviceInfo } from "../lib/device";
import BrightnessCard from "./BrightnessCard";
import PlaceholderCard from "./PlaceholderCard";
import TimezoneCard from "./TimezoneCard";
import WifiCard from "./WifiCard";

interface Props {
  device: Device;
  info: DeviceInfo;
  onDisconnect: () => void;
}

type Page = "general" | "clock";

/** 配置页：顶栏（状态/固件/断开）+ 侧栏导航 + 面板（通用配置 / App 占位） */
export default function ConfigScreen({ device, info, onDisconnect }: Props) {
  const [page, setPage] = useState<Page>("general");
  const [cfg, setCfg] = useState<DeviceConfig | null>(null);
  const [error, setError] = useState("");

  useEffect(() => {
    device
      .getCfg()
      .then(setCfg)
      .catch((e) => setError(e instanceof Error ? e.message : String(e)));
  }, [device]);

  const saveWifi = async (ssid: string, pass: string) => {
    setCfg(await device.setCfg(pass ? { wifi_ssid: ssid, wifi_pass: pass } : { wifi_ssid: ssid }));
  };
  const saveBrightness = async (value: number) => {
    setCfg(await device.setCfg({ brightness: value }));
  };
  const saveTz = async (tz: string) => {
    setCfg(await device.setCfg({ tz }));
  };

  return (
    <div className="app">
      <header className="topbar">
        <span className="dot ok" title="已连接" />
        <span className="title">{info.name}</span>
        <span className="fw muted">固件 {info.fw}</span>
        <span className="spacer" />
        <button className="btn ghost" onClick={onDisconnect}>
          断开连接
        </button>
      </header>
      <div className="layout">
        <nav className="sidebar">
          <button
            className={`nav-item${page === "general" ? " active" : ""}`}
            onClick={() => setPage("general")}
          >
            通用配置
          </button>
          <button
            className={`nav-item${page === "clock" ? " active" : ""}`}
            onClick={() => setPage("clock")}
          >
            时钟 <span className="badge">即将上线</span>
          </button>
        </nav>
        <main className="panel">
          {error && <div className="banner err">{error}</div>}
          {page === "clock" ? (
            <PlaceholderCard title="时钟配置" />
          ) : cfg ? (
            <>
              <div className="panel-head">
                <h1>通用配置</h1>
                <p className="muted">Wi-Fi、屏幕亮度与时区，保存即生效</p>
              </div>
              <WifiCard cfg={cfg} onSave={saveWifi} />
              <BrightnessCard brightness={cfg.brightness} onChange={saveBrightness} />
              <TimezoneCard tz={cfg.tz} onSave={saveTz} />
            </>
          ) : (
            !error && <p className="muted">读取配置中…</p>
          )}
        </main>
      </div>
    </div>
  );
}
