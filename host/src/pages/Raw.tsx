import { useState } from "react";

import * as api from "../api";
import { STATUS_LABELS } from "../constants";
import type { Apply, RawReply } from "../types";

interface Props {
  apply: Apply;
}

function parseNumber(text: string): number {
  const trimmed = text.trim();
  if (trimmed === "") {
    throw new Error("empty number");
  }
  const value = /^0x/i.test(trimmed)
    ? Number.parseInt(trimmed.slice(2), 16)
    : Number.parseInt(trimmed, 10);
  if (Number.isNaN(value)) {
    throw new Error(`cannot parse number ${JSON.stringify(text)}`);
  }
  return value;
}

function parsePayload(text: string): number[] {
  const parts = text.split(/[\s,]+/).filter((part) => part !== "");
  return parts.map((part) => {
    const value = parseNumber(part);
    if (value < 0 || value > 255) {
      throw new Error(`payload byte out of range: ${part}`);
    }
    return value;
  });
}

function hex(bytes: number[]): string {
  return bytes.map((b) => b.toString(16).padStart(2, "0")).join(" ");
}

export default function Raw({ apply }: Props) {
  const [cmd, setCmd] = useState("0x01");
  const [payload, setPayload] = useState("");
  const [result, setResult] = useState<RawReply | null>(null);
  const [error, setError] = useState<string | null>(null);

  const send = () => {
    let cmdValue: number;
    let bytes: number[];
    try {
      cmdValue = parseNumber(cmd);
      bytes = parsePayload(payload);
    } catch (e) {
      setError(String(e));
      return;
    }
    if (cmdValue < 0 || cmdValue > 255) {
      setError(`command out of range: ${cmd}`);
      return;
    }
    setError(null);
    void apply(async () => {
      const reply = await api.raw(cmdValue, bytes);
      setResult(reply);
    });
  };

  return (
    <div>
      <h2>Raw console</h2>

      <section className="card">
        <h3>Request</h3>
        <div className="row">
          <span className="link-label">cmd</span>
          <input
            value={cmd}
            onChange={(e) => setCmd(e.target.value)}
            placeholder="0x01 or 1"
          />
        </div>
        <div className="row">
          <span className="link-label">payload</span>
          <input
            className="grow"
            value={payload}
            onChange={(e) => setPayload(e.target.value)}
            placeholder="hex bytes, e.g. 40 06"
          />
        </div>
        <button type="button" onClick={send}>
          Send
        </button>
        {error && <p className="error-inline">{error}</p>}
      </section>

      {result && (
        <section className="card">
          <h3>Reply</h3>
          <dl className="grid">
            <div>
              <dt>Status</dt>
              <dd>
                0x{result.status.toString(16).padStart(2, "0")} ·{" "}
                {STATUS_LABELS[result.status] ?? result.status_label}
              </dd>
            </div>
            <div>
              <dt>Data</dt>
              <dd>
                <code>{result.data.length ? hex(result.data) : "(empty)"}</code>
              </dd>
            </div>
          </dl>
        </section>
      )}
    </div>
  );
}
