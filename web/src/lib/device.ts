/*
 * 设备通信层：Web Serial 真机实现 + localStorage mock 实现（同一 Device 接口）。
 * 协议（proto 3）：UTF-8 行文本，命令行 PING/GETCFG/SETCFG <json>/SCAN/TIME/WAKE；
 * 响应行 # 前缀（#HELLO/#CFG/#ERR/#NETWORKS/#TIME），其余行（ESP_LOG）为噪声直接丢弃。
 * 规格见 docs/features/web-config.md。
 */

export interface DeviceInfo {
  name: string;
  fw: string;
  proto: number;
}

export interface DeviceConfig {
  brightness: number; // 0-100
  idle_timeout?: number; // 自动低功耗秒数 {0,30,60,300,600}，0=永不；旧固件（proto<3）无此字段
  tz: string;
  wifi_ssid: string;
  wifi_pass_set: boolean;
  ntp_server: string;
  alarm_mode?: number; // 闹钟模式 0=关 1=单次 2=每周 3=每天；旧固件（proto<4）无闹钟字段
  alarm_hour?: number; // 0-23
  alarm_min?: number; // 0-59
  alarm_weekdays?: number; // 位掩码 bit0=周一 … bit6=周日（每周模式生效）
}

export interface ConfigPatch {
  brightness?: number;
  idle_timeout?: number;
  tz?: string;
  wifi_ssid?: string;
  wifi_pass?: string;
  ntp_server?: string;
  alarm_mode?: number;
  alarm_hour?: number;
  alarm_min?: number;
  alarm_weekdays?: number;
}

/** SCAN 结果条目（按信号强度降序，未隐藏 AP） */
export interface WifiNetwork {
  ssid: string;
  rssi: number;
  secure: boolean;
}

/** TIME 响应：本次运行是否已 NTP 同步 + 设备侧 UTC/本地时间 */
export interface TimeInfo {
  synced: boolean;
  utc: number;
  local: string;
}

export interface Device {
  /** 授权（浏览器弹窗）+ 打开串口 + PING 握手；失败抛错（message 为原因短语） */
  connect(): Promise<DeviceInfo>;
  disconnect(): Promise<void>;
  getCfg(): Promise<DeviceConfig>;
  /** 部分更新；任一字段非法时设备整体拒绝，此处抛错；成功返回新状态 */
  setCfg(patch: ConfigPatch): Promise<DeviceConfig>;
  /** 扫描附近未隐藏 AP（设备阻塞扫描 ~2-4s，超时 15s；结果按信号强度降序） */
  scan(): Promise<WifiNetwork[]>;
  /** 查询设备时间与同步状态 */
  getTime(): Promise<TimeInfo>;
  /** 设备拔线 / 保活连续 2 次失败 / disconnect 事件时回调（只触发一次） */
  onDisconnect(cb: () => void): void;
}

/** URL 带 mock=1 时用模拟设备（无硬件演示/开发）；真机需 Chromium 系浏览器 */
export function createDevice(): Device {
  const mock = new URLSearchParams(window.location.search).has("mock");
  return mock ? new MockDevice() : new SerialDevice();
}

/* ---------------- 真机：Web Serial ---------------- */

const VID_ESPRESSIF = 0x303a;
const HANDSHAKE_TIMEOUT_MS = 3000;
const HANDSHAKE_RETRIES = 2; // 共 3 次尝试
const HEARTBEAT_MS = 5000;
const HEARTBEAT_MISS_LIMIT = 2;

type Waiter = {
  tag: string;
  resolve: (line: string) => void;
  reject: (err: Error) => void;
  timer: number;
};

class SerialDevice implements Device {
  private port: SerialPort | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private decoder = new TextDecoder("utf-8");
  private lineBuf = "";
  private waiter: Waiter | null = null;
  private chain: Promise<unknown> = Promise.resolve();
  private heartbeatTimer: number | null = null;
  private heartbeatMisses = 0;
  private disconnectCbs: Array<() => void> = [];
  private disconnectedFired = false;
  private teardownPromise: Promise<void> | null = null;

  onDisconnect(cb: () => void): void {
    this.disconnectCbs.push(cb);
  }

  async connect(): Promise<DeviceInfo> {
    if (!("serial" in navigator)) {
      throw new Error("当前浏览器不支持 Web Serial，请使用 Chrome 或 Edge");
    }
    this.disconnectedFired = false; // 复位断开状态与 teardown 单例，支持实例复用重连
    this.teardownPromise = null;
    try {
      this.port = await navigator.serial.requestPort({
        filters: [{ usbVendorId: VID_ESPRESSIF }],
      });
      await this.port.open({ baudRate: 115200 });
    } catch (e) {
      this.port = null;
      throw new Error("未选择设备或串口打开失败（可能被其他程序占用）");
    }

    this.writer = this.port.writable!.getWriter();
    this.startReader();
    navigator.serial.addEventListener("disconnect", this.onSerialDisconnect);

    let lastErr: unknown = null;
    for (let i = 0; i <= HANDSHAKE_RETRIES; i++) {
      try {
        const line = await this.request("PING", "#HELLO", HANDSHAKE_TIMEOUT_MS);
        const info = JSON.parse(line.slice("#HELLO ".length)) as DeviceInfo;
        this.startHeartbeat();
        return info;
      } catch (e) {
        lastErr = e;
      }
    }
    /* 握手失败：从未成功连接，置位以不发 onDisconnect 回调，
       也不让 pump 退出时再触发一次（teardown 本身已由单例保证只跑一次） */
    this.disconnectedFired = true;
    await this.teardown();
    throw new Error(`配对失败：设备无响应（${String(lastErr)}）`);
  }

  async disconnect(): Promise<void> {
    /* 置位标志：pump 退出时不再触发 onDisconnect 回调（主动断开由调用方自行复位 UI） */
    this.disconnectedFired = true;
    await this.teardown();
  }

  async getCfg(): Promise<DeviceConfig> {
    const line = await this.enqueue(() => this.request("GETCFG", "#CFG", 3000));
    return JSON.parse(line.slice("#CFG ".length)) as DeviceConfig;
  }

  async setCfg(patch: ConfigPatch): Promise<DeviceConfig> {
    const line = await this.enqueue(() =>
      this.request(`SETCFG ${JSON.stringify(patch)}`, "#CFG", 3000),
    );
    return JSON.parse(line.slice("#CFG ".length)) as DeviceConfig;
  }

  async scan(): Promise<WifiNetwork[]> {
    /* 设备阻塞扫描 ~2-4s：长超时；请求经 enqueue 串行，心跳 PING 排队其后不受影响 */
    const line = await this.enqueue(() => this.request("SCAN", "#NETWORKS", 15000));
    return JSON.parse(line.slice("#NETWORKS ".length)) as WifiNetwork[];
  }

  async getTime(): Promise<TimeInfo> {
    const line = await this.enqueue(() => this.request("TIME", "#TIME", 3000));
    return JSON.parse(line.slice("#TIME ".length)) as TimeInfo;
  }

  /* ---- 内部 ---- */

  /** 请求串行化：同一时刻仅一个在途请求（响应按到达顺序配对） */
  private enqueue<T>(fn: () => Promise<T>): Promise<T> {
    const p = this.chain.then(fn, fn);
    this.chain = p.catch(() => {});
    return p;
  }

  private async writeLine(cmd: string): Promise<void> {
    if (this.writer == null) throw new Error("未连接");
    await this.writer.write(new TextEncoder().encode(cmd + "\n"));
  }

  /** 写命令并等待 tag 匹配的 # 响应行（#ERR 拒绝任何在途请求） */
  private request(cmd: string, tag: string, timeoutMs: number): Promise<string> {
    return new Promise<string>((resolve, reject) => {
      this.waiter = {
        tag,
        resolve,
        reject,
        timer: window.setTimeout(() => {
          if (this.waiter) this.waiter = null;
          reject(new Error("设备响应超时"));
        }, timeoutMs),
      };
      this.writeLine(cmd).catch((e) => {
        if (this.waiter) {
          clearTimeout(this.waiter.timer);
          this.waiter = null;
        }
        reject(e);
      });
    });
  }

  private startReader(): void {
    const reader = this.port!.readable!.getReader();
    this.reader = reader;
    const pump = async () => {
      try {
        for (;;) {
          const { done, value } = await reader.read();
          if (done) break;
          this.lineBuf += this.decoder.decode(value, { stream: true });
          let nl: number;
          while ((nl = this.lineBuf.indexOf("\n")) >= 0) {
            const line = this.lineBuf.slice(0, nl).trim();
            this.lineBuf = this.lineBuf.slice(nl + 1);
            this.handleLine(line);
          }
        }
      } catch {
        /* reader.cancel 或串口异常关闭 */
      }
      if (!this.disconnectedFired) this.fireDisconnect();
    };
    void pump();
  }

  private handleLine(line: string): void {
    if (!line.startsWith("#")) return; // ESP_LOG 噪声
    const waiter = this.waiter;
    if (waiter == null) return;
    if (line.startsWith(waiter.tag)) {
      clearTimeout(waiter.timer);
      this.waiter = null;
      waiter.resolve(line);
    } else if (line.startsWith("#ERR")) {
      clearTimeout(waiter.timer);
      this.waiter = null;
      waiter.reject(new Error(line.slice("#ERR ".length) || "设备返回错误"));
    }
    // 其他 # 行（如迟到的心跳响应）与当前请求不匹配：丢弃
  }

  private startHeartbeat(): void {
    this.heartbeatTimer = window.setInterval(() => {
      void this.enqueue(() => this.request("PING", "#HELLO", 2500))
        .then(() => (this.heartbeatMisses = 0))
        .catch(() => {
          if (++this.heartbeatMisses >= HEARTBEAT_MISS_LIMIT) this.fireDisconnect();
        });
    }, HEARTBEAT_MS);
  }

  private onSerialDisconnect = (ev: Event): void => {
    if ((ev as SerialDisconnectEvent).port === this.port) this.fireDisconnect();
  };

  private fireDisconnect(): void {
    if (this.disconnectedFired) return;
    this.disconnectedFired = true;
    void this.teardown();
    for (const cb of this.disconnectCbs) cb();
  }

  /** 幂等单例：connect 失败 / 主动断开 / 拔线多路汇入时 teardown 仅执行一次——
      并发的第二次 teardown 会在 reader 锁尚未释放时 port.close()（被拒且吞掉），
      且抢先置空 this.port 使第一次的 close 永远丢失，串口保持打开 */
  private teardown(): Promise<void> {
    this.teardownPromise ??= this.doTeardown();
    return this.teardownPromise;
  }

  private async doTeardown(): Promise<void> {
    if (this.heartbeatTimer != null) clearInterval(this.heartbeatTimer);
    this.heartbeatTimer = null;
    if (this.waiter) {
      clearTimeout(this.waiter.timer);
      this.waiter.reject(new Error("连接已断开"));
      this.waiter = null;
    }
    navigator.serial.removeEventListener("disconnect", this.onSerialDisconnect);
    /* Web Serial 关闭顺序（MDN）：cancel 让 read() 以 done 退出但不释放锁，
       必须 releaseLock 解锁 readable/writable 后 port.close() 才会真正关闭串口 */
    const reader = this.reader;
    this.reader = null;
    try {
      await reader?.cancel();
    } catch {
      /* 流已 errored（如拔线） */
    }
    try {
      reader?.releaseLock();
    } catch {
      /* 已释放 */
    }
    try {
      this.writer?.releaseLock();
    } catch {
      /* 写入进行中或已释放 */
    }
    this.writer = null;
    const port = this.port;
    this.port = null;
    try {
      await port?.close();
    } catch {
      /* 已关闭 */
    }
  }
}

/* ---------------- Mock：localStorage 模拟设备 ---------------- */

const MOCK_KEY = "cubebox.mock.cfg";
const MOCK_DELAY_MS = 150;

type MockState = Required<ConfigPatch>;

const MOCK_DEFAULTS: MockState = {
  brightness: 80,
  idle_timeout: 30,
  tz: "CST-8",
  wifi_ssid: "",
  wifi_pass: "",
  ntp_server: "ntp.aliyun.com",
  alarm_mode: 0,
  alarm_hour: 7,
  alarm_min: 0,
  alarm_weekdays: 127,
};

/** 与固件 config_model 一致的白名单（秒；0=永不） */
const IDLE_TIMEOUT_WHITELIST = [0, 30, 60, 300, 600];

/** 与固件 config_model 相同的验证规则（ssid/pass 允许可打印 ASCII 与合法 UTF-8，字节计长） */
function mockValidate(patch: ConfigPatch): void {
  if (patch.brightness !== undefined) {
    if (!Number.isInteger(patch.brightness) || patch.brightness < 0 || patch.brightness > 100)
      throw new Error("bad field: brightness");
  }
  if (patch.idle_timeout !== undefined) {
    if (!IDLE_TIMEOUT_WHITELIST.includes(patch.idle_timeout))
      throw new Error("bad field: idle_timeout");
  }
  if (patch.tz !== undefined) {
    if (!/^[A-Za-z0-9+\-,./:<>]{1,39}$/.test(patch.tz)) throw new Error("bad field: tz");
  }
  const byteLen = (s: string) => new TextEncoder().encode(s).length;
  const noControl = (s: string) => !/[\x00-\x1f\x7f]/.test(s);
  if (patch.wifi_ssid !== undefined) {
    const n = byteLen(patch.wifi_ssid);
    if (n < 1 || n > 32 || !noControl(patch.wifi_ssid)) throw new Error("bad field: wifi_ssid");
  }
  if (patch.wifi_pass !== undefined) {
    if (byteLen(patch.wifi_pass) > 64 || !noControl(patch.wifi_pass))
      throw new Error("bad field: wifi_pass");
  }
  if (patch.ntp_server !== undefined) {
    if (!/^[A-Za-z0-9.:-]{1,63}$/.test(patch.ntp_server)) throw new Error("bad field: ntp_server");
  }
  if (patch.alarm_mode !== undefined) {
    if (!Number.isInteger(patch.alarm_mode) || patch.alarm_mode < 0 || patch.alarm_mode > 3)
      throw new Error("bad field: alarm_mode");
  }
  if (patch.alarm_hour !== undefined) {
    if (!Number.isInteger(patch.alarm_hour) || patch.alarm_hour < 0 || patch.alarm_hour > 23)
      throw new Error("bad field: alarm_hour");
  }
  if (patch.alarm_min !== undefined) {
    if (!Number.isInteger(patch.alarm_min) || patch.alarm_min < 0 || patch.alarm_min > 59)
      throw new Error("bad field: alarm_min");
  }
  if (patch.alarm_weekdays !== undefined) {
    if (!Number.isInteger(patch.alarm_weekdays) || patch.alarm_weekdays < 0 || patch.alarm_weekdays > 127)
      throw new Error("bad field: alarm_weekdays");
  }
}

class MockDevice implements Device {
  private disconnectCbs: Array<() => void> = [];
  private connected = false;

  onDisconnect(cb: () => void): void {
    this.disconnectCbs.push(cb);
  }

  async connect(): Promise<DeviceInfo> {
    await sleep(MOCK_DELAY_MS);
    this.connected = true;
    return { name: "CubeBox", fw: "0.3.0-mock", proto: 4 };
  }

  async disconnect(): Promise<void> {
    this.connected = false;
  }

  async getCfg(): Promise<DeviceConfig> {
    this.assertConnected();
    await sleep(MOCK_DELAY_MS);
    const s = this.load();
    return {
      brightness: s.brightness,
      idle_timeout: s.idle_timeout,
      tz: s.tz,
      wifi_ssid: s.wifi_ssid,
      wifi_pass_set: s.wifi_pass !== "",
      ntp_server: s.ntp_server,
      alarm_mode: s.alarm_mode,
      alarm_hour: s.alarm_hour,
      alarm_min: s.alarm_min,
      alarm_weekdays: s.alarm_weekdays,
    };
  }

  async setCfg(patch: ConfigPatch): Promise<DeviceConfig> {
    this.assertConnected();
    await sleep(MOCK_DELAY_MS);
    mockValidate(patch);
    const s = { ...this.load(), ...patch } as MockState;
    localStorage.setItem(MOCK_KEY, JSON.stringify(s));
    return {
      brightness: s.brightness,
      idle_timeout: s.idle_timeout,
      tz: s.tz,
      wifi_ssid: s.wifi_ssid,
      wifi_pass_set: s.wifi_pass !== "",
      ntp_server: s.ntp_server,
      alarm_mode: s.alarm_mode,
      alarm_hour: s.alarm_hour,
      alarm_min: s.alarm_min,
      alarm_weekdays: s.alarm_weekdays,
    };
  }

  async scan(): Promise<WifiNetwork[]> {
    this.assertConnected();
    await sleep(800); /* 模拟设备阻塞扫描 */
    return [
      { ssid: "Home_5G", rssi: -46, secure: true },
      { ssid: "Home_2.4G", rssi: -58, secure: true },
      { ssid: "邻居家的 Wi-Fi", rssi: -72, secure: true },
      { ssid: "CoffeeShop-Free", rssi: -81, secure: false },
    ];
  }

  async getTime(): Promise<TimeInfo> {
    this.assertConnected();
    await sleep(MOCK_DELAY_MS);
    const s = this.load();
    const now = new Date();
    const p2 = (n: number) => String(n).padStart(2, "0");
    return {
      /* 演示语义：配好 Wi-Fi + NTP 服务器即视为已同步 */
      synced: s.wifi_ssid !== "" && s.ntp_server !== "",
      utc: Math.floor(now.getTime() / 1000),
      local: `${now.getFullYear()}-${p2(now.getMonth() + 1)}-${p2(now.getDate())} ${p2(now.getHours())}:${p2(now.getMinutes())}:${p2(now.getSeconds())}`,
    };
  }

  private load(): MockState {
    try {
      const raw = localStorage.getItem(MOCK_KEY);
      if (raw) return { ...MOCK_DEFAULTS, ...(JSON.parse(raw) as MockState) };
    } catch {
      /* 损坏数据回退默认 */
    }
    return { ...MOCK_DEFAULTS };
  }

  private assertConnected(): void {
    if (!this.connected) throw new Error("未连接");
  }
}

function sleep(ms: number): Promise<void> {
  return new Promise((r) => setTimeout(r, ms));
}
