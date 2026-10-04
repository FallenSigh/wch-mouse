# wch-mouse host

A Tauri v2 desktop application that is the PC-side configuration and status
client for the [wch-mouse](../README.md) firmware. It talks to the mouse over
its vendor HID **Feature report** channel (usage page `0xFF00`, Report ID
`0x02`, 63 data bytes), either wired (product id `0xFE0C`) or wirelessly through the
2.4 GHz dongle (product id `0xFE0D`), where a command crosses the RF link and
its answer comes back longer.

It is the graphical counterpart of [`../scripts/mouse_proto.py`](../scripts/mouse_proto.py);
the wire format in `src-tauri/src/protocol.rs` mirrors that script and
`../src/app/proto.c`.

**Scope of this first version:** every configuration get/set, a status
dashboard, and a live BIO monitor (heart-rate waveform plus JFC103 metrics,
wired only). No presets.

## Layout

```
host/
  package.json            pnpm + Vite + React + TS frontend
  vite.config.ts          Tauri conventions (port 1420, strictPort, no clearScreen)
  src/
    api.ts                typed `invoke` wrappers (one per Tauri command)
    types.ts              TS mirrors of the Rust DTOs
    constants.ts          rate / air-mouse tables
    App.tsx               sidebar + content shell
    pages/                Overview, Sensor, AirMouse, Lighting, DisplayBio, BioMonitor, Raw
  src-tauri/
    Cargo.toml            tauri 2, serde, serde_json, hidapi, serialport
    build.rs              tauri-build
    tauri.conf.json       v2 schema
    capabilities/         core:default capability
    icons/                placeholder bundle icons
    src/
      protocol.rs         PURE, dependency-free frame/reply codec + unit tests
      bio_frame.rs        PURE, dependency-free JFC103 packet parser + framer + tests
      bio.rs              BIO CDC serial enumeration + reader thread + serde facade
      transport.rs        hidapi feature send/read + the transact poll loop
      device.rs           enumerate, connect, typed method per command
      commands.rs         #[tauri::command] handlers
      error.rs            shared error type
      lib.rs / main.rs    Tauri builder + entry point
```

## Fedora system dependencies

Tauri v2 needs the WebKitGTK 4.1 / GTK3 / libsoup3 development packages:

```sh
sudo dnf install webkit2gtk4.1-devel libsoup3-devel gtk3-devel \
                 openssl-devel librsvg2-devel systemd-devel
sudo dnf group install "C Development Tools and Libraries"
```

`systemd-devel` provides libudev, which the serialport crate uses to enumerate
USB serial ports. You also need the Rust toolchain (1.77+) and Node 22 + pnpm 10.
The hidapi crate uses its **default static hidraw** backend on Linux, so no
libusb package is required; a C compiler (already installed above) builds the
bundled library.
On Windows and macOS the same crate resolves to the native HID backends
(`hid.dll` / IOKit) with no extra setup.

## Install & run

```sh
cd host
pnpm install
pnpm tauri dev      # or: cargo tauri dev, if you have cargo-tauri
pnpm tauri build    # bundle for the current platform
```

`pnpm dev` alone runs only the Vite frontend; the desktop window is started by
`pnpm tauri dev`, which reuses the dev server on port 1420.

## Device permissions (udev)

The kernel exposes `/dev/hidrawN` as `root:root` mode `0600` and the wired
mouse's CDC serial (`/dev/ttyACM*`, the BIO stream) as `root:dialout` mode
`0660`, so the app cannot open either as a normal user until the rule shipped in
this repo is installed (hidraw for both product ids, tty for the wired mouse):

```sh
sudo cp scripts/99-wch-mouse.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Re-plug the device if it is already attached. Without this the device picker
lists the interfaces but `connect` fails with a permission error, and the BIO
monitor's `Start` cannot open the serial port.

## BIO monitor

The **BIO Monitor** page streams the health module's real-time output.

- **Wired only.** The mouse is a composite device: faces 0/1 are a USB CDC
  serial port (`/dev/ttyACM*` on Linux) carrying the module's 88-byte
  `0xFF`-headed JFC103 packets at **38400 8N1**. The dongle (`0xFE0D`) has no
  CDC interface, so it cannot serve BIO — when only the dongle is attached the
  port list is empty and the page says to connect the mouse by USB.
- **Start** first sends `bio_sleep(false)` and `bio_acq(true)` over the HID
  Feature channel (exactly the Display & BIO switches), then opens the serial
  port and reads it on a background thread. **Stop** ends the reader and sends
  `bio_acq(false)`. Read errors are surfaced as an inline message.
- A hand-rolled `<canvas>` draws the last ~10 s (512 samples) of the 64-sample
  `acdata` waveform; the metrics grid shows the JFC103 fields (heart rate, SpO₂,
  microcirculation, fatigue, blood pressure, cardiac output, peripheral
  resistance, RR/SDNN/RMSSD/NN50/PNN50 and the `rra` series). An all-zero packet
  is a "no contact" reading and is shown as 无接触 / 无数据.

## Testing the protocol without the GUI dependencies

`src-tauri/src/protocol.rs` is deliberately dependency-free, so it can be
tested in a bare crate even when webkit2gtk is not installed:

```sh
mkdir -p /tmp/proto-check/src
cp host/src-tauri/src/protocol.rs /tmp/proto-check/src/lib.rs
cat > /tmp/proto-check/Cargo.toml <<'EOF'
[package]
name = "proto-check"
version = "0.1.0"
edition = "2021"
EOF
cargo test --manifest-path /tmp/proto-check/Cargo.toml
```

This covers the known `SET_DPI` frame vector, `sum8` behaviour, reply parsing,
checksum/length rejection, status/command round-trips and the report-id strip.

`src-tauri/src/bio_frame.rs` is likewise std-only, so the JFC103 parser can be
tested the same way (`cp host/src-tauri/src/bio_frame.rs /tmp/bio-check/src/lib.rs`).
This covers field decoding, leading-garbage resync, consecutive frames and the
all-zero "no contact" `valid` flag, without the serialport/Tauri layer.

## Notes

- The codebase pins no CI for this app yet; the firmware workflow in
  `.github/workflows/` is untouched.
- Nothing under `host/` modifies the firmware. The RF path uses the same
  protocol as the wired path, just with the longer (400 ms) reply timeout,
  selected automatically when the dongle is the target.
- The webkit2gtk / libsoup3 development packages are **not** installed in the
  build sandbox this app was scaffolded in, so `cargo check` for `src-tauri`
  stops at the `webkit2gtk-sys` pkg-config probe. The `protocol.rs` and
  `bio_frame.rs` tests were still run in isolation (see above).
