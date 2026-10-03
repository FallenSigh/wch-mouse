import { useCallback, useEffect, useState } from "react";

import * as api from "./api";
import AirMouse from "./pages/AirMouse";
import BioMonitor from "./pages/BioMonitor";
import DisplayBio from "./pages/DisplayBio";
import Lighting from "./pages/Lighting";
import Overview from "./pages/Overview";
import Raw from "./pages/Raw";
import Sensor from "./pages/Sensor";
import type { Settings, Status } from "./types";

export type PageId =
  | "overview"
  | "sensor"
  | "air"
  | "lighting"
  | "display"
  | "bio"
  | "raw";

const NAV: { id: PageId; label: string }[] = [
  { id: "overview", label: "Overview" },
  { id: "sensor", label: "Sensor" },
  { id: "air", label: "Air Mouse" },
  { id: "lighting", label: "Lighting" },
  { id: "display", label: "Display & BIO" },
  { id: "bio", label: "BIO Monitor" },
  { id: "raw", label: "Raw console" },
];

export default function App() {
  const [page, setPage] = useState<PageId>("overview");
  const [connected, setConnected] = useState(false);
  const [status, setStatus] = useState<Status | null>(null);
  const [settings, setSettings] = useState<Settings | null>(null);
  const [error, setError] = useState<string | null>(null);

  const refresh = useCallback(async () => {
    try {
      const [nextSettings, nextStatus] = await Promise.all([
        api.getSettings(),
        api.getStatus(),
      ]);
      setSettings(nextSettings);
      setStatus(nextStatus);
      setError(null);
    } catch (e) {
      setError(String(e));
    }
  }, []);

  const handleConnected = useCallback(async () => {
    setConnected(true);
    await refresh();
  }, [refresh]);

  const handleDisconnect = useCallback(async () => {
    try {
      await api.disconnect();
    } catch (e) {
      setError(String(e));
    }
    setConnected(false);
    setSettings(null);
    setStatus(null);
  }, []);

  const apply = useCallback(
    async (action: () => Promise<unknown>) => {
      try {
        await action();
        await refresh();
      } catch (e) {
        setError(String(e));
      }
    },
    [refresh]
  );

  useEffect(() => {
    if (connected) {
      void refresh();
    }
  }, [connected, refresh]);

  return (
    <div className="app">
      <aside className="sidebar">
        <h1>wch-mouse</h1>
        <nav>
          {NAV.map((item) => (
            <button
              key={item.id}
              type="button"
              className={item.id === page ? "active" : ""}
              disabled={item.id !== "overview" && !connected}
              onClick={() => setPage(item.id)}
            >
              {item.label}
            </button>
          ))}
        </nav>
        <div className={`conn ${connected ? "on" : "off"}`}>
          {connected ? "connected" : "disconnected"}
        </div>
      </aside>

      <main className="content">
        {error && (
          <div className="error" onClick={() => setError(null)}>
            <span>{error}</span>
            <button type="button" aria-label="dismiss">
              ×
            </button>
          </div>
        )}

        {page === "overview" && (
          <Overview
            connected={connected}
            status={status}
            onConnected={handleConnected}
            onDisconnect={handleDisconnect}
            onRefresh={refresh}
          />
        )}

        {page === "sensor" && settings && (
          <Sensor settings={settings} apply={apply} />
        )}
        {page === "air" && settings && (
          <AirMouse settings={settings} apply={apply} />
        )}
        {page === "lighting" && settings && (
          <Lighting settings={settings} apply={apply} />
        )}
        {page === "display" && settings && (
          <DisplayBio settings={settings} apply={apply} />
        )}
        {page === "bio" && <BioMonitor />}
        {page === "raw" && settings && <Raw apply={apply} />}

        {page !== "overview" && page !== "bio" && !settings && (
          <p className="muted">Connect a device first.</p>
        )}
      </main>
    </div>
  );
}
