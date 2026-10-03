// Owns the BIO session above the page tree so the serial reader keeps streaming
// when the user navigates away from the monitor. The Rust side already keeps the
// reader thread alive between pages; this makes the frontend match it and keeps
// the waveform / latest packet across navigation.
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useRef,
  useState,
  type MutableRefObject,
  type ReactNode,
} from "react";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";

import * as api from "./api";
import type { BioPacket, SerialPortInfo } from "./types";

/** Rolling waveform length: ~10 s at 64 samples per 1.28 s packet. */
const RING = 512;

interface BioContextValue {
  ports: SerialPortInfo[];
  port: string | null;
  setPort: (port: string) => void;
  running: boolean;
  packet: BioPacket | null;
  error: string | null;
  samples: MutableRefObject<number[]>;
  refreshPorts: () => Promise<void>;
  start: () => Promise<void>;
  stop: () => Promise<void>;
}

const BioContext = createContext<BioContextValue | null>(null);

export function useBio(): BioContextValue {
  const ctx = useContext(BioContext);
  if (!ctx) {
    throw new Error("useBio must be used inside <BioProvider>");
  }
  return ctx;
}

export function BioProvider({ children }: { children: ReactNode }) {
  const [ports, setPorts] = useState<SerialPortInfo[]>([]);
  const [port, setPort] = useState<string | null>(null);
  const [running, setRunning] = useState(false);
  const [packet, setPacket] = useState<BioPacket | null>(null);
  const [error, setError] = useState<string | null>(null);

  const samples = useRef<number[]>([]);
  const unlisteners = useRef<UnlistenFn[]>([]);
  const runningRef = useRef(false);

  const clearListeners = useCallback(() => {
    for (const un of unlisteners.current) un();
    unlisteners.current = [];
  }, []);

  const onPacket = useCallback((next: BioPacket) => {
    const buf = samples.current;
    for (const sample of next.acdata) buf.push(sample);
    if (buf.length > RING) buf.splice(0, buf.length - RING);
    setPacket(next);
  }, []);

  const refreshPorts = useCallback(async () => {
    try {
      const list = await api.listSerialPorts();
      setPorts(list);
      setPort((current) =>
        current && list.some((p) => p.port_name === current)
          ? current
          : (list[0]?.port_name ?? null)
      );
      setError(null);
    } catch (e) {
      setError(String(e));
    }
  }, []);

  const stop = useCallback(async () => {
    runningRef.current = false;
    clearListeners();
    setRunning(false);
    try {
      await api.bioStop();
    } catch (e) {
      setError(String(e));
    }
  }, [clearListeners]);

  const start = useCallback(async () => {
    if (!port) {
      return;
    }
    setError(null);
    try {
      await api.bioStart(port);
      clearListeners();
      unlisteners.current.push(
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
    void refreshPorts();
    return () => {
      clearListeners();
      if (runningRef.current) {
        runningRef.current = false;
        void api.bioStop().catch(() => undefined);
      }
    };
  }, [refreshPorts, clearListeners]);

  const value: BioContextValue = {
    ports,
    port,
    setPort,
    running,
    packet,
    error,
    samples,
    refreshPorts,
    start,
    stop,
  };

  return <BioContext.Provider value={value}>{children}</BioContext.Provider>;
}
