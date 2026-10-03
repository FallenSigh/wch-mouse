import { useState } from "react";

import * as api from "../api";
import type { Apply, Settings } from "../types";

interface Props {
  settings: Settings;
  apply: Apply;
}

export default function DisplayBio({ settings, apply }: Props) {
  const [buzzMs, setBuzzMs] = useState(200);

  const safeBuzz = Math.max(0, Math.min(30000, Math.round(buzzMs)));

  return (
    <div>
      <h2>Display &amp; BIO</h2>

      <section className="card">
        <h3>OLED panel</h3>
        <label className="row">
          <input
            type="checkbox"
            checked={settings.oled}
            onChange={(e) => void apply(() => api.setOled(e.target.checked))}
          />
          Panel on
        </label>
      </section>

      <section className="card">
        <h3>BIO module</h3>
        <div className="row">
          <button type="button" onClick={() => void apply(() => api.bioAcq(true))}>
            Start acquisition
          </button>
          <button
            type="button"
            onClick={() => void apply(() => api.bioAcq(false))}
          >
            Stop acquisition
          </button>
        </div>
        <div className="row">
          <button
            type="button"
            onClick={() => void apply(() => api.bioSleep(true))}
          >
            Sleep
          </button>
          <button
            type="button"
            onClick={() => void apply(() => api.bioSleep(false))}
          >
            Wake
          </button>
        </div>
        <p className="muted">
          The BIO commands have no read-back; the last command is fire-and-forget.
        </p>
      </section>

      <section className="card">
        <h3>Peripherals on battery</h3>
        <label className="row">
          <input
            type="checkbox"
            checked={settings.periph_batt}
            onChange={(e) => void apply(() => api.setPeriph(e.target.checked))}
          />
          Allow panel / LED rail / BIO to run on battery
        </label>
      </section>

      <section className="card">
        <h3>Vibration motor</h3>
        <label className="row">
          <input
            type="checkbox"
            checked={settings.motor_enable}
            onChange={(e) =>
              void apply(() => api.setMotorEn(e.target.checked))
            }
          />
          Motor enabled
        </label>
        <div className="row">
          <input
            type="number"
            min={0}
            max={30000}
            step={50}
            value={buzzMs}
            onChange={(e) => setBuzzMs(Number(e.target.value))}
          />
          <span className="muted">ms (max 30000)</span>
          <button type="button" onClick={() => void apply(() => api.buzz(safeBuzz))}>
            Buzz
          </button>
        </div>
        <p className="muted">
          Running now: {settings.motor_running ? "yes" : "no"}
        </p>
      </section>
    </div>
  );
}
