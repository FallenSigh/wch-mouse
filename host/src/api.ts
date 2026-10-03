// Typed wrappers around the Tauri commands. Command names and argument keys
// must match src-tauri/src/commands.rs (Tauri v2 converts camelCase JS keys to
// snake_case Rust parameters automatically).
import { invoke } from "@tauri-apps/api/core";

import type {
  DeviceInfo,
  DeviceKind,
  RawReply,
  Rgb,
  SerialPortInfo,
  Settings,
  Status,
} from "./types";

export const listDevices = () => invoke<DeviceInfo[]>("list_devices");

export const connect = (pidKind: DeviceKind, path?: string | null) =>
  invoke<void>("connect", { pidKind, path: path ?? null });

export const disconnect = () => invoke<void>("disconnect");

export const getStatus = () => invoke<Status>("status");

export const getSettings = () => invoke<Settings>("get_settings");

export const setDpi = (cpi: number) => invoke<number>("set_dpi", { cpi });

export const setSensor = (mode: number) => invoke<number>("set_sensor", { mode });

export const setLift = (cut: number) => invoke<number>("set_lift", { cut });

export const setAirSens = (idx: number) =>
  invoke<number>("set_air_sens", { idx });

export const setAirOdr = (idx: number) => invoke<number>("set_air_odr", { idx });

export const setRgb = (rgb: Rgb) => invoke<Rgb>("set_rgb", { rgb });

export const setOled = (on: boolean) => invoke<boolean>("set_oled", { on });

export const setRate = (link: number, hz: number) =>
  invoke<number>("set_rate", { link, hz });

export const setPeriph = (on: boolean) => invoke<boolean>("set_periph", { on });

export const setMotorEn = (on: boolean) =>
  invoke<boolean>("set_motor_en", { on });

export const buzz = (ms: number) => invoke<boolean>("buzz", { ms });

export const bioAcq = (on: boolean) => invoke<void>("bio_acq", { on });

export const bioSleep = (on: boolean) => invoke<void>("bio_sleep", { on });

export const listSerialPorts = () =>
  invoke<SerialPortInfo[]>("list_serial_ports");

export const bioStart = (port: string) => invoke<void>("bio_start", { port });

export const bioStop = () => invoke<void>("bio_stop");

export const raw = (cmd: number, payload: number[]) =>
  invoke<RawReply>("raw", { cmd, payload });
