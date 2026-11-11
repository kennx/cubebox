/* Task 3 最小连通页：验证 device 层（mock / 真机）。Task 4 换完整 UI。 */
import { useRef, useState } from "react";
import {
  createDevice,
  type Device,
  type DeviceConfig,
  type DeviceInfo,
} from "./lib/device";

export default function App() {
  const [state, setState] = useState<"idle" | "connecting" | "connected">("idle");
  const [info, setInfo] = useState<DeviceInfo | null>(null);
  const [cfg, setCfg] = useState<DeviceConfig | null>(null);
  const [err, setErr] = useState("");
  const deviceRef = useRef<Device | null>(null);

  const onConnect = async () => {
    setErr("");
    setState("connecting");
    try {
      const d = createDevice();
      d.onDisconnect(() => {
        setState("idle");
        setCfg(null);
        setInfo(null);
      });
      const i = await d.connect();
      deviceRef.current = d;
      setInfo(i);
      setCfg(await d.getCfg());
      setState("connected");
    } catch (e) {
      setErr(e instanceof Error ? e.message : String(e));
      setState("idle");
    }
  };

  return (
    <main style={{ fontFamily: "system-ui", padding: 24 }}>
      <h1>CubeBox 配置（脚手架页）</h1>
      {state !== "connected" ? (
        <>
          <button onClick={onConnect} disabled={state === "connecting"}>
            {state === "connecting" ? "连接中…" : "授权并连接"}
          </button>
          {err && <p style={{ color: "#c0392b" }}>{err}</p>}
        </>
      ) : (
        <div>
          <p>已连接 {info?.name} fw={info?.fw}</p>
          <pre>{JSON.stringify(cfg, null, 2)}</pre>
          <button
            onClick={async () => setCfg(await deviceRef.current!.getCfg())}
          >
            getCfg()
          </button>
          <button onClick={() => deviceRef.current?.disconnect()}>断开</button>
        </div>
      )}
    </main>
  );
}
