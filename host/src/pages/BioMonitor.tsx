import { useCallback, useEffect, useRef, useState } from "react";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";

import * as api from "../api";
import type { BioPacket, SerialPortInfo } from "../types";

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
  const [ports, setPorts] = useState<SerialPortInfo[]>([]);
  const [port, setPort] = useState<string | null>(null);
  const [running, setRunning] = useState(false);
  const [packet, setPacket] = useState<BioPacket | null>(null);
  const [error, setError] = useState<string | null>(null);

  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const samplesRef = useRef<number[]>([]);
  const unlistenersRef = useRef<UnlistenFn[]>([]);
  const runningRef = useRef(false);

  const clearListeners = useCallback(() => {
    for (const un of unlistenersRef.current) un();
    unlistenersRef.current = [];
  }, []);

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

    const data = samplesRef.current;
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
  }, []);

  const onPacket = useCallback(
    (next: BioPacket) => {
      setPacket(next);
      const buf = samplesRef.current;
      for (const sample of next.acdata) buf.push(sample);
      if (buf.length > RING) buf.splice(0, buf.length - RING);
      draw();
    },
    [draw]
  );

  const loadPorts = useCallback(async () => {
    try {
      const list = await api.listSerialPorts();
      setPorts(list);
      setPort((current) =>
        current && list.some((p) => p.port_name === current)
          ? current
          : (list[0]?.port_name ?? null)
      );
    } catch (e) {
      setError(String(e));
    }
  }, []);

  const handleStop = useCallback(async () => {
    runningRef.current = false;
    clearListeners();
    try {
      await api.bioStop();
    } catch (e) {
      setError(String(e));
    }
    setRunning(false);
  }, [clearListeners]);

  const handleStart = useCallback(async () => {
    if (!port) return;
    setError(null);
    try {
      await api.bioStart(port);
      clearListeners();
      unlistenersRef.current.push(
        await listen<BioPacket>("bio:packet", (e) => onPacket(e.payload)),
        await listen<string>("bio:error", (e) => setError(e.payload))
      );
      runningRef.current = true;
      setRunning(true);
    } catch (e) {
      setError(String(e));
    }
  }, [port, onPacket, clearListeners]);

  useEffect(() => {
    void loadPorts();
    const onResize = () => draw();
    window.addEventListener("resize", onResize);
    return () => {
      window.removeEventListener("resize", onResize);
      clearListeners();
      if (runningRef.current) {
        runningRef.current = false;
        void api.bioStop().catch(() => undefined);
      }
    };
  }, [loadPorts, draw, clearListeners]);

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
            onClick={() => void handleStart()}
          >
            Start
          </button>
          <button type="button" disabled={!running} onClick={() => void handleStop()}>
            Stop
          </button>
          <button type="button" disabled={running} onClick={() => void loadPorts()}>
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
