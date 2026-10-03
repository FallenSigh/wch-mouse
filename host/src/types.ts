// TypeScript mirrors of the Rust DTOs in src-tauri/src/device.rs.

export type DeviceKind = "mouse" | "dongle";

export interface DeviceInfo {
  kind: DeviceKind;
  path: string;
  product: string | null;
  manufacturer: string | null;
  serial: string | null;
  interface_number: number;
  usage_page: number;
  usage: number;
  is_vendor_interface: boolean;
}

export interface Version {
  protocol: number;
  fw_major: number;
  fw_minor: number;
  fw_patch: number;
}

export interface Capabilities {
  raw: number;
  sensor: boolean;
  radio: boolean;
  battery: boolean;
  bio: boolean;
}

export interface Battery {
  voltage_mv: number;
  percent: number;
  charging: boolean;
  power_good: boolean;
  fault: boolean;
}

export interface Rgb {
  enable: number;
  effect: number;
  brightness: number;
  r: number;
  g: number;
  b: number;
}

export interface Status {
  version: Version;
  capabilities: Capabilities;
  radio: string;
  battery: Battery;
}

export interface Rates {
  usb: number;
  rf: number;
  ble: number;
}

export interface Settings {
  version: Version;
  capabilities: Capabilities;
  radio: string;
  battery: Battery;
  dpi: number;
  sensor: number;
  sensor_label: string;
  lift: number;
  lift_label: string;
  air_sens: number;
  air_sens_pair: [number, number];
  air_odr: number;
  air_odr_hz: number;
  rgb: Rgb;
  oled: boolean;
  periph_batt: boolean;
  motor_enable: boolean;
  motor_running: boolean;
  rates: Rates;
}

export interface RawReply {
  status: number;
  status_label: string;
  data: number[];
}

export type LinkName = "usb" | "rf" | "ble";

/** One wired mouse CDC serial port (see `bio.rs`). */
export interface SerialPortInfo {
  port_name: string;
  product: string | null;
  serial_number: string | null;
}

/** One JFC103 real-time packet. Field names mirror the Rust DTO exactly. */
export interface BioPacket {
  acdata: number[];
  heartrate: number;
  spo2: number;
  bk: number;
  fatigue: number;
  systolic: number;
  diastolic: number;
  cardiac_output: number;
  peripheral_resistance: number;
  rr: number;
  sdnn: number;
  rmssd: number;
  nn50: number;
  pnn50: number;
  rra: number[];
  valid: boolean;
}

/**
 * Run a mutating action and refresh the aggregate settings afterwards.
 * Errors are surfaced by the caller (App).
 */
export type Apply = (action: () => Promise<unknown>) => Promise<void>;

/** Link selector codes as the protocol expects them. */
export const LINK_CODE: Record<LinkName, number> = {
  usb: 0,
  rf: 1,
  ble: 2,
};
