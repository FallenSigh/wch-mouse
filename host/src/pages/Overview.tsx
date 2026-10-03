import { useCallback, useEffect, useState } from "react";

import * as api from "../api";
import type { DeviceInfo, Status } from "../types";

interface Props {
  connected: boolean;
  status: Status | null;
  onConnected: () => Promise<void>;
  onDisconnect: () => Promise<void>;
  onRefresh: () => Promise<void>;
}

export default function Overview({
  connected,
  status,
  onConnected,
  onDisconnect,
  onRefresh,
}: Props) {
  const [devices, setDevices] = useState<DeviceInfo[]>([]);
  const [loading, setLoading] = useState(false);
  const [busy, setBusy] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const reload = useCallback(async () => {
    setLoading(true);
    try {
      setDevices(await api.listDevices());
      setError(null);
    } catch (e) {
      setError(String(e));
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void reload();
  }, [reload]);

  const connectTo = async (device: DeviceInfo) => {
    setBusy(device.path);
    try {
      await api.connect(device.kind, device.path);
      await onConnected();
      setError(null);
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(null);
    }
  };

  return (
    <div>
      <div className="page-head">
        <h2>Overview</h2>
        <button type="button" onClick={() => void reload()} disabled={loading}>
          {loading ? "Scanning…" : "Rescan"}
        </button>
      </div>

      <section className="card">
        <h3>Devices</h3>
        {devices.length === 0 && (
          <p className="muted">
            No wch-mouse (0x1a86:0xfe0c) or dongle (0x1a86:0xfe0d) HID interface
            found. Plug the device in and rescan.
          </p>
        )}
        <ul className="device-list">
          {devices.map((device) => (
            <li key={device.path}>
              <div className="device-info">
                <strong>{device.kind}</strong>
                {device.is_vendor_interface && (
                  <span className="badge">vendor itf</span>
                )}
                <span className="muted">
                  itf {device.interface_number} · usage{" "}
                  {device.usage_page.toString(16)}:
                  {device.usage.toString(16)}
                </span>
                <code>{device.path}</code>
                {device.product && <span>{device.product}</span>}
              </div>
              <button
                type="button"
                onClick={() => void connectTo(device)}
                disabled={busy !== null}
              >
                {busy === device.path ? "Connecting…" : "Connect"}
              </button>
            </li>
          ))}
        </ul>

        {connected && (
          <div className="row">
            <button
              type="button"
              className="danger"
              onClick={() => void onDisconnect()}
            >
              Disconnect
            </button>
            <button type="button" onClick={() => void onRefresh()}>
              Refresh status
            </button>
          </div>
        )}
        {error && <p className="error-inline">{error}</p>}
      </section>

      {status && (
        <>
          <section className="card">
            <h3>Status</h3>
            <dl className="grid">
              <div>
                <dt>Firmware</dt>
                <dd>
                  {status.version.fw_major}.{status.version.fw_minor}.
                  {status.version.fw_patch}
                </dd>
              </div>
              <div>
                <dt>Protocol</dt>
                <dd>v{status.version.protocol}</dd>
              </div>
              <div>
                <dt>Radio</dt>
                <dd>{status.radio}</dd>
              </div>
              <div>
                <dt>Battery</dt>
                <dd>
                  {status.battery.percent}% · {status.battery.voltage_mv} mV
                </dd>
              </div>
            </dl>
            <p className="muted">
              {status.battery.charging && "charging "}
              {status.battery.power_good && "power-good "}
              {status.battery.fault && "charge-fault"}
              {!status.battery.charging &&
                !status.battery.power_good &&
                !status.battery.fault &&
                "on battery, not charging"}
            </p>
          </section>

          <section className="card">
            <h3>Capabilities</h3>
            <ul className="caps">
              <li className={status.capabilities.sensor ? "yes" : "no"}>
                sensor
              </li>
              <li className={status.capabilities.radio ? "yes" : "no"}>
                radio
              </li>
              <li className={status.capabilities.battery ? "yes" : "no"}>
                battery
              </li>
              <li className={status.capabilities.bio ? "yes" : "no"}>bio</li>
            </ul>
            <p className="muted">
              raw bitmap 0x{status.capabilities.raw.toString(16).padStart(4, "0")}
            </p>
          </section>
        </>
      )}
    </div>
  );
}
