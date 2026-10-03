import { useCallback, useEffect, useRef } from "react";

import { useBio } from "../BioContext";

const RING = 512;
const WAVE_MIN = -128;
const WAVE_MAX = 127;

type NumericKey =
  | "heartrate"
  | "spo2"
  | "bk"
  | "fatigue"
  | "systolic"
  | "diastolic"
  | "cardiac_output"
  | "peripheral_resistance"
  | "rr"
  | "sdnn"
  | "rmssd"
  | "nn50"
  | "pnn50";

const METRICS: { key: NumericKey; label: string; unit?: string }[] = [
  { key: "heartrate", label: "心率", unit: "bpm" },
  { key: "spo2", label: "血氧", unit: "%" },
  { key: "bk", label: "微循环" },
  { key: "fatigue", label: "疲劳指数" },
  { key: "systolic", label: "收缩压", unit: "mmHg" },
  { key: "diastolic", label: "舒张压", unit: "mmHg" },
  { key: "cardiac_output", label: "心输出" },
  { key: "peripheral_resistance", label: "外周阻力" },
  { key: "rr", label: "RR间期", unit: "ms" },
  { key: "sdnn", label: "SDNN", unit: "ms" },
  { key: "rmssd", label: "RMSSD", unit: "ms" },
  { key: "nn50", label: "NN50" },
  { key: "pnn50", label: "PNN50", unit: "%" },
];

export default function BioMonitor() {
  const { ports, port, setPort, running, packet, error, samples, refreshPorts, start, stop } =
    useBio();
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    const pw = Math.max(1, Math.round(w * dpr));
    const ph = Math.max(1, Math.round(h * dpr));
    if (canvas.width !== pw || canvas.height !== ph) {
      canvas.width = pw;
      canvas.height = ph;
    }

    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.fillStyle = "#14151a";
    ctx.fillRect(0, 0, w, h);

    ctx.strokeStyle = "#2a2d36";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, h / 2);
    ctx.lineTo(w, h / 2);
    ctx.stroke();

    const data = samples.current;
    if (data.length < 2) return;

    const half = Math.max(1, h / 2 - 6);
    ctx.beginPath();
    ctx.strokeStyle = "#4f8cff";
    ctx.lineWidth = 1.5;
    for (let i = 0; i < data.length; i++) {
      const x = (i / (RING - 1)) * w;
      const v = Math.max(WAVE_MIN, Math.min(WAVE_MAX, data[i]));
      const y = h / 2 - (v / 128) * half;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }, [samples]);

  useEffect(() => {
    draw();
  }, [draw, packet]);

  useEffect(() => {
    const onResize = () => draw();
    window.addEventListener("resize", onResize);
    return () => window.removeEventListener("resize", onResize);
  }, [draw]);

  const hasContact = packet?.valid ?? false;

  return (
    <div>
      <h2>BIO Monitor</h2>

      {error && <p className="error-inline">{error}</p>}

      <section className="card">
        <h3>Serial port</h3>
        {ports.length === 0 ? (
          <p className="muted">
            Connect the mouse by USB (the dongle has no serial port).
          </p>
        ) : (
          <div className="row">
            <select
              value={port ?? ""}
              disabled={running}
              onChange={(e) => setPort(e.target.value)}
            >
              {ports.map((p) => (
                <option key={p.port_name} value={p.port_name}>
                  {p.port_name}
                  {p.product ? ` — ${p.product}` : ""}
                </option>
              ))}
            </select>
          </div>
        )}
        <div className="row">
          <button
            type="button"
            disabled={running || !port}
            onClick={() => void start()}
          >
            Start
          </button>
          <button type="button" disabled={!running} onClick={() => void stop()}>
            Stop
          </button>
          <button type="button" disabled={running} onClick={() => void refreshPorts()}>
            Refresh ports
          </button>
          <span className="muted">
            {running ? (hasContact ? "receiving" : "waiting for contact") : "stopped"}
          </span>
        </div>
      </section>

      <section className="card">
        <h3>Waveform</h3>
        <canvas
          ref={canvasRef}
          style={{ width: "100%", height: 200, display: "block" }}
        />
      </section>

      <section className="card">
        <h3>Metrics</h3>
        {!packet ? (
          <p className="muted">无数据</p>
        ) : !packet.valid ? (
          <p className="muted">无接触 / 无数据</p>
        ) : (
          <>
            <dl className="grid">
              {METRICS.map((m) => (
                <div key={m.key}>
                  <dt>{m.label}</dt>
                  <dd>
                    {packet[m.key]}
                    {m.unit ? ` ${m.unit}` : ""}
                  </dd>
                </div>
              ))}
            </dl>
            <p className="muted">rra: {packet.rra.join(", ")}</p>
          </>
        )}
      </section>
    </div>
  );
}
