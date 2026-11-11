import type { MouseEvent } from "react";

interface Props {
  busy: boolean;
  error: string;
  onConnect: () => void;
}

/** 未连接落地页：提示插线 + 授权连接；不支持 Web Serial 时禁用并引导换浏览器 */
export default function ConnectScreen({ busy, error, onConnect }: Props) {
  const supported = typeof navigator !== "undefined" && "serial" in navigator;

  const click = (e: MouseEvent<HTMLButtonElement>) => {
    e.currentTarget.blur();
    onConnect();
  };

  return (
    <div className="connect">
      <div className="connect-card">
        <div className="logo" aria-hidden="true">
          <svg width="40" height="40" viewBox="0 0 32 32">
            <rect width="32" height="32" rx="7" fill="#0f1115" />
            <circle cx="16" cy="16" r="6" fill="#4f8cff" />
          </svg>
        </div>
        <h1>CubeBox 配置</h1>
        <p className="muted">使用 USB 线将设备连接到电脑，然后授权连接</p>
        <button className="btn primary lg" disabled={busy || !supported} onClick={click}>
          {busy ? "连接中…" : "授权并连接"}
        </button>
        {!supported && (
          <p className="hint err-text">当前浏览器不支持 Web Serial，请使用 Chrome 或 Edge 打开</p>
        )}
        {error && (
          <p className="hint err-text">
            {error}
            <br />
            <span className="muted">请确认设备已插入且串口未被其他程序占用，然后重试</span>
          </p>
        )}
        <p className="foot muted">仅支持 USB 连接 · Chrome / Edge</p>
      </div>
    </div>
  );
}
