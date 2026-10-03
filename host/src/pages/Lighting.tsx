import { useEffect, useState } from "react";

import * as api from "../api";
import { EFFECTS } from "../constants";
import type { Apply, Rgb, Settings } from "../types";

interface Props {
  settings: Settings;
  apply: Apply;
}

function toHex(rgb: Rgb): string {
  const part = (value: number) => value.toString(16).padStart(2, "0");
  return `#${part(rgb.r)}${part(rgb.g)}${part(rgb.b)}`;
}

function fromHex(hex: string): [number, number, number] {
  const clean = hex.replace("#", "");
  if (!/^[0-9a-fA-F]{6}$/.test(clean)) {
    return [0, 0, 0];
  }
  return [
    Number.parseInt(clean.slice(0, 2), 16),
    Number.parseInt(clean.slice(2, 4), 16),
    Number.parseInt(clean.slice(4, 6), 16),
  ];
}

export default function Lighting({ settings, apply }: Props) {
  const rgb = settings.rgb;
  const [enable, setEnable] = useState(rgb.enable !== 0);
  const [effect, setEffect] = useState(rgb.effect);
  const [brightness, setBrightness] = useState(rgb.brightness);
  const [color, setColor] = useState(toHex(rgb));

  useEffect(() => {
    setEnable(rgb.enable !== 0);
    setEffect(rgb.effect);
    setBrightness(rgb.brightness);
    setColor(toHex(rgb));
  }, [rgb.enable, rgb.effect, rgb.brightness, rgb.r, rgb.g, rgb.b]);

  const submit = () => {
    const [r, g, b] = fromHex(color);
    void apply(() =>
      api.setRgb({
        enable: enable ? 1 : 0,
        effect,
        brightness,
        r,
        g,
        b,
      })
    );
  };

  return (
    <div>
      <h2>Lighting</h2>

      <section className="card">
        <h3>Underglow</h3>
        <label className="row">
          <input
            type="checkbox"
            checked={enable}
            onChange={(e) => setEnable(e.target.checked)}
          />
          Enabled
        </label>

        <div className="row">
          <span className="link-label">Effect</span>
          <select
            value={effect}
            onChange={(e) => setEffect(Number(e.target.value))}
          >
            {EFFECTS.map((label, index) => (
              <option key={label} value={index}>
                {label}
              </option>
            ))}
          </select>
        </div>

        <div className="row">
          <span className="link-label">Brightness</span>
          <input
            type="range"
            min={1}
            max={255}
            value={brightness}
            onChange={(e) => setBrightness(Number(e.target.value))}
          />
          <input
            type="number"
            min={1}
            max={255}
            value={brightness}
            onChange={(e) => setBrightness(Number(e.target.value))}
          />
        </div>

        <div className="row">
          <span className="link-label">Colour</span>
          <input
            type="color"
            value={color}
            onChange={(e) => setColor(e.target.value)}
          />
          <code>{color}</code>
        </div>

        <button type="button" onClick={submit}>
          Apply
        </button>
      </section>

      <section className="card">
        <h3>Current</h3>
        <dl className="grid">
          <div>
            <dt>Enable</dt>
            <dd>{rgb.enable ? "on" : "off"}</dd>
          </div>
          <div>
            <dt>Effect</dt>
            <dd>{EFFECTS[rgb.effect] ?? `unknown (${rgb.effect})`}</dd>
          </div>
          <div>
            <dt>Brightness</dt>
            <dd>{rgb.brightness}</dd>
          </div>
          <div>
            <dt>Colour</dt>
            <dd>
              <span
                className="swatch"
                style={{ background: toHex(rgb) }}
                aria-hidden
              />
              {toHex(rgb)}
            </dd>
          </div>
        </dl>
      </section>
    </div>
  );
}
