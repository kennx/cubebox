/* 最小 Web Serial 类型声明（TS lib.dom 尚未收录；仅本项目用到的子集） */

interface SerialPortInfo {
  usbVendorId?: number;
  usbProductId?: number;
}

interface SerialOptions {
  baudRate: number;
}

interface SerialPort {
  readonly readable: ReadableStream<Uint8Array> | null;
  readonly writable: WritableStream<Uint8Array> | null;
  open(options: SerialOptions): Promise<void>;
  close(): Promise<void>;
}

interface SerialConnectEvent extends Event {
  readonly port: SerialPort;
}

interface SerialDisconnectEvent extends Event {
  readonly port: SerialPort;
}

interface Serial extends EventTarget {
  requestPort(options?: { filters?: SerialPortInfo[] }): Promise<SerialPort>;
  getPorts(): Promise<SerialPort[]>;
  addEventListener(
    type: "connect" | "disconnect",
    listener: (ev: SerialConnectEvent | SerialDisconnectEvent) => void,
  ): void;
  removeEventListener(
    type: "connect" | "disconnect",
    listener: (ev: SerialConnectEvent | SerialDisconnectEvent) => void,
  ): void;
}

interface Navigator {
  readonly serial: Serial;
}
