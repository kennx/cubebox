import { useCallback, useState } from "react";
import ConfigScreen from "./components/ConfigScreen";
import ConnectScreen from "./components/ConnectScreen";
import { createDevice, type Device, type DeviceInfo } from "./lib/device";

type Phase = "idle" | "connecting" | "connected";

export default function App() {
  const [phase, setPhase] = useState<Phase>("idle");
  const [error, setError] = useState("");
  const [device, setDevice] = useState<Device | null>(null);
  const [info, setInfo] = useState<DeviceInfo | null>(null);

  const reset = useCallback(() => {
    setPhase("idle");
    setDevice(null);
    setInfo(null);
  }, []);

  const handleConnect = useCallback(async () => {
    setError("");
    setPhase("connecting");
    try {
      const d = createDevice();
      d.onDisconnect(reset); // 拔线 / 保活失败 → 回连接页
      const i = await d.connect();
      setDevice(d);
      setInfo(i);
      setPhase("connected");
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
      setPhase("idle");
    }
  }, [reset]);

  const handleDisconnect = useCallback(async () => {
    /* 等串口真正关闭再回连接页，避免断开后立即重连时端口尚未释放 */
    await device?.disconnect();
    reset();
  }, [device, reset]);

  if (phase === "connected" && device && info) {
    return <ConfigScreen device={device} info={info} onDisconnect={handleDisconnect} />;
  }
  return <ConnectScreen busy={phase === "connecting"} error={error} onConnect={handleConnect} />;
}
