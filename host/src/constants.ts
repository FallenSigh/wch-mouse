// Protocol tables mirrored from src-tauri/src/protocol.rs / scripts/mouse_proto.py.

export const RATES = [125, 250, 500, 1000, 2000, 4000, 8000] as const;

export const SENSOR_MODES = [
  "high-performance",
  "low-power",
  "office",
  "corded-gaming",
] as const;

export const LIFT_CUTS = ["1mm", "2mm"] as const;

export const AIR_SENS = [
  [9, 8],
  [18, 16],
  [27, 24],
  [36, 32],
  [54, 48],
  [72, 64],
  [108, 96],
] as const;

export const AIR_ODR_HZ = [25, 50, 100, 200, 400, 800, 1600, 3200] as const;

export const EFFECTS = ["hue cycle", "solid"] as const;

export const STATUS_LABELS: Record<number, string> = {
  0: "OK",
  1: "unknown command",
  2: "bad length",
  3: "bad checksum",
  4: "unsupported",
};
