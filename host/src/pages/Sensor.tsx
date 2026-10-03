import { useEffect, useState } from "react";

import * as api from "../api";
import { LIFT_CUTS, RATES, SENSOR_MODES } from "../constants";
import type { Apply, LinkName, Settings } from "../types";

interface Props {
  settings: Settings;
  apply: Apply;
}

const LINKS: { name: LinkName; code: number; rate: (s: Settings) => number }[] = [
  { name: "usb", code: 0, rate: (s) => s.rates.usb },
  { name: "rf", code: 1, rate: (s) => s.rates.rf },
  { name: "ble", code: 2, rate: (s) => s.rates.ble },
];

export default function Sensor({ settings, apply }: Props) {
  const [dpi, setDpi] = useState(settings.dpi);
  const [mode, setMode] = useState(settings.sensor);
  const [lift, setLift] = useState(settings.lift);

  useEffect(() => setDpi(settings.dpi), [settings.dpi]);
  useEffect(() => setMode(settings.sensor), [settings.sensor]);
  useEffect(() => setLift(settings.lift), [settings.lift]);

  const clampDpi = (value: number) =>
    Math.min(26000, Math.max(50, Math.round(value)));

  return (
    <div>
      <h2>Sensor</h2>

      <section className="card">
        <h3>CPI / DPI</h3>
        <div className="row">
          <input
            type="range"
            min={50}
            max={26000}
            step={50}
            value={dpi}
            onChange={(e) => setDpi(Number(e.target.value))}
          />
          <input
            type="number"
            min={50}
            max={26000}
            step={50}
            value={dpi}
            onChange={(e) => setDpi(Number(e.target.value))}
          />
          <button
            type="button"
            onClick={() =>
              void apply(async () => ({ dpi: await api.setDpi(clampDpi(dpi)) }))
            }
          >
            Apply
          </button>
        </div>
        <p className="muted">Device reports {settings.dpi} CPI.</p>
      </section>

      <section className="card">
        <h3>Power mode</h3>
        <div className="row">
          <select
            value={mode}
            onChange={(e) => setMode(Number(e.target.value))}
          >
            {SENSOR_MODES.map((label, index) => (
              <option key={label} value={index}>
                {index} · {label}
              </option>
            ))}
          </select>
          <button
            type="button"
            onClick={() =>
              void apply(async () => {
                const sensor = await api.setSensor(mode);
                return { sensor, sensor_label: SENSOR_MODES[sensor] };
              })
            }
          >
            Apply
          </button>
        </div>
      </section>

      <section className="card">
        <h3>Lift-off</h3>
        <div className="row">
          <select
            value={lift}
            onChange={(e) => setLift(Number(e.target.value))}
          >
            {LIFT_CUTS.map((label, index) => (
              <option key={label} value={index}>
                {label}
              </option>
            ))}
          </select>
          <button
            type="button"
            onClick={() =>
              void apply(async () => {
                const cut = await api.setLift(lift);
                return { lift: cut, lift_label: LIFT_CUTS[cut] };
              })
            }
          >
            Apply
          </button>
        </div>
      </section>

      <section className="card">
        <h3>Report rate</h3>
        {LINKS.map((link) => (
          <div className="row" key={link.name}>
            <span className="link-label">{link.name}</span>
            <select
              value={link.rate(settings)}
              onChange={(e) =>
                void apply(async () => {
                  const hz = await api.setRate(link.code, Number(e.target.value));
                  return { rates: { ...settings.rates, [link.name]: hz } };
                })
              }
            >
              {RATES.map((hz) => (
                <option key={hz} value={hz}>
                  {hz} Hz
                </option>
              ))}
            </select>
          </div>
        ))}
      </section>
    </div>
  );
}
