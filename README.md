# wch-mouse

[English](README.md) | [简体中文](README.zh-CN.md)

Bare-metal firmware for a wireless gaming mouse and its 2.4 GHz receiver dongle,
running on the **WCH CH585** (RISC-V RV32IMC + Zba/Zbb/Zbc/Zbs/Xw).

One CMake project builds two images:

| Target | Firmware | Sources |
| --- | --- | --- |
| `wch-mouse` | the mouse | `src/` |
| `wch-dongle` | the 2.4 GHz receiver (RF host → USB HID) | `dongle/` |

Both are linked from a shared vendor SDK / HAL / USB device / RF link setup in
`cmake/wch_sdk.cmake`, so a driver or descriptor change reaches both.

## Features

- **Three links, one radio.** USB is always available and wins whenever a host
  is attached. Otherwise the mouse runs either the 2.4 GHz RF link (low
  latency, needs the dongle) or a Bluetooth LE HID peripheral — a single
  hardware radio, so the mode is chosen at boot, not switched live.
- **PAW3395** optical sensor with interrupt-driven motion reads over SPI0.
- **BMI270** IMU driving an *air-mouse* mode: hold side-1 + side-2 to hand the
  cursor from the optical sensor to the gyro, and back.
- **SSD1315** OLED status panel, **WS2812** underglow, **ERM** vibration motor.
- **Battery**: BQ24075 charger + VBAT monitoring, with a deep standby path.
- **Host configuration** over a vendor HID Feature report — through USB, or
  wirelessly through the dongle.

## Hardware

| Block | Part / interface |
| --- | --- |
| MCU | WCH CH585, 448 KiB flash @ `0x0`, 128 KiB RAM @ `0x20000000` |
| Optical sensor | PAW3395 on SPI0 (motion on the PA7 falling edge, SPI0 DMA) |
| IMU | BMI270 on SPI1 (air-mouse gyro only) |
| Display | SSD1315 OLED on I2C (`PB20`/`PB21`) |
| Underglow | WS2812 (TIM1 + DMA) |
| Motor | ERM vibration motor |
| Charger | BQ24075 (`PGOOD` `PB6` / `CHG` `PB5`) + `BT_VOL` ADC (`PA3`) |
| Health module | external BIO module on UART3 (`JFC103` framing) |

The full pin map is in [`docs/io.csv`](docs/io.csv) and mirrored in
[`src/bsp/board.h`](src/bsp/board.h), which is the single place to edit wiring.

## Repository layout

```
src/               mouse application code
  app/             mouse, air-mouse, radio mode, battery, motor, BIO, settings, protocol
  bsp/             board pin map (board.h), non-volatile store (nvm), USB HID device, UART, logging
  display/         I2C bus, SSD1315 panel glue + vendored LibDriver core
  led/             WS2812 driver and RGB animations
  sensor/          PAW3395 driver + BMI270/BMI2 (vendored Bosch API) and their ports
  transport/       transport router + USB / RF / BLE backends, shared rf_cfg.h air contract
dongle/            2.4 GHz receiver firmware (RF host -> USB HID)
cmake/             shared build: source lists, flags, linker options, toolchain
ble/               vendor-forked BLE HAL + GATT profiles + prebuilt closed-source libraries
StdPeriphDriver/   vendor peripheral driver (do not edit)
Startup/ RVMSIS/   vendor startup assembly and CMSIS-like headers (do not edit)
Ld/Link.ld         custom linker script
docs/              datasheets + io.csv pin map (the 75 MB vendor SDK under docs/CH585EVT is local-only)
scripts/           host tooling (mouse_proto.py), MounRiver flash helper, udev rule
```

## Getting started

### Prerequisites

- The **MounRiver / WCH RISC-V GCC toolchain** (`riscv32-wch-elf-gcc`). A plain
  `cmake -B build` fails by design: the build `FATAL_ERROR`s without a toolchain
  file. Toolchain discovery order is `$WCH_TOOLCHAIN_ROOT`, then
  `/opt/wch/riscv-wch-gcc15`, then `~/MounRiver/...`; override with
  `-DWCH_TOOLCHAIN_ROOT=/path`.
- CMake ≥ 3.21, plus Ninja (or Make).
- `wchisp` on `PATH` for the simple flash targets.
- Python 3 + `hidapi` (`pip install hidapi`) for the host configuration tool.

### Build

```sh
cmake --preset wch          # toolchain + build/ configured
cmake --build build         # both firmwares -> build/wch-mouse.* and build/dongle/wch-dongle.*

cmake --build build --target wch-mouse     # or just one
cmake --build build --target wch-dongle
```

### Configuration options

- `WCH_BLE_ENABLE` — link the BLE (peripheral) transport.
- `WCH_RF_ENABLE` — include the 2.4 GHz RF transport. BLE and RF share the single
  radio, so enabling both builds a dual-radio image that picks one at boot.
- `WCH_DCDC_ENABLE` — enable the internal DC-DC; requires the 10 µH inductor
  between `VSW` and `VDCID`, and must stay `OFF` on boards that strap them together.
- `WCH_DEBUG_UART` — retarget `printf` to a debug UART (`-DWCH_DEBUG_UART=1`).
- `WCH_LOG_LEVEL` — log verbosity (`-DWCH_LOG_LEVEL=LOG_LVL_DEBUG`).

`compile_commands.json` is a symlink into `build/`, so run the configure step
before expecting clangd/LSP to work on `src/`.

### Flash

Flashing uses external tools and is **not** part of the default build. The mouse
owns the unsuffixed target names; the dongle's are suffixed `-dongle`:

```sh
cmake --build build --target flash-usb          # wchisp -u flash
cmake --build build --target flash-serial       # wchisp -s flash
cmake --build build --target flash-mrs          # MounRiver CommunicationLib (libmcuupdate.so)

cmake --build build --target flash-usb-dongle   # same targets for the dongle
```

`flash-mrs` drives the same native library MounRiver Studio uses, so it works
even when the two-wire debug interface is closed.

The mouse can reach the ROM ISP bootloader **without a power cycle**: hold the
BOOT strap (**PB22**) for 3 s while USB is attached. This erases block 0 first,
so the app image does not survive and a download must follow — but the DataFlash
settings record does. Gated on VBUS, so a press on battery cannot wipe the image.
The dongle has no button and uses the power-cycle flow.

## Host tooling

The mouse exposes a vendor-defined HID **Feature report** (usage page `0xFF00`,
no Report ID, 63 data bytes). Because HID gives the host no payload on a
`GET_FEATURE`, every transaction is a `SET_FEATURE` request followed by a
`GET_FEATURE` read of the answer the device prepared.

```sh
pip install hidapi

# let the logged-in user reach the hidraw nodes (both product ids)
sudo cp scripts/99-wch-mouse.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=hidraw

# wired (product id 0xFE0C)
scripts/mouse_proto.py info
scripts/mouse_proto.py dpi 1600
scripts/mouse_proto.py rate 1000

# wireless, through the dongle (product id 0xFE0D)
scripts/mouse_proto.py --dongle info
```

Run `scripts/mouse_proto.py --help` for the full list: `version`, `info`,
`radio`, `battery`, `dpi`, `sensor`, `lift`, `air-sens`, `air-odr`, `rgb`,
`oled`, `rate`, `bio-acq`, `bio-sleep`, `periph`, `motor`, `motor-en` and
`raw`. The get/set commands work while the mouse runs RF: the dongle relays the
request and carries the answer back in the mouse's own frames.

## Architecture

### Transport router

`src/transport.c` holds an array of `transport_t` backends; the first whose
`link_up()` is true wins, in priority order **USB > RF > BLE**. Backends live in
`src/transport/`: `transport_usb.c`, `transport_rf.c` (RF builds) and
`transport_ble.c` (BLE builds). Both radio files always compile and stub out
when their define is absent.

### 2.4 GHz RF link

`src/transport/rf_cfg.h` is the on-air contract shared by both firmwares: the
mouse is the device/TX and the dongle is the host/RX. The dongle answers every
packet with a short ACK carrying its received count, so the mouse can see air
loss; the same reverse channel carries host SET commands to the mouse and the
mouse's answers back to the dongle, which serves them to a `GET_FEATURE`.

### Radio modes

The mode lives in DataFlash and is applied at **boot** (the vendor stack exposes
no proven live switch). Hold **side-1 + the wheel click for 3 s, then release**
to store the other mode and reset into it. A single-radio build fixes its mode
at compile time and ignores the stored byte, so a stale record cannot disable
the only backend that build carries. USB is independent of the mode.

### Configuration and persistence

User settings (DPI, sensor mode, lift-off, RGB, OLED, report rates, air-mouse
presets, motor enable, peripheral-on-battery policy) are kept in DataFlash via
the generic record store in `src/bsp/nvm.c`, which owns the ROM `EEPROM_*`
offset/alignment/block-erase quirks. `src/app/settings.c` owns the record and
applies it to the hardware; callers read it through accessors.

### Power management

After 30 s idle — on battery, not in air-mouse mode, and on RF — the peripherals
are parked and the core enters a 2.6 µA standby (`src/app/power.c`). Wake sources
are the motion pin, the buttons/encoder and the charger. The panel, LED rail and
BIO module are the largest standing loads and run only while the cable is in
unless the `periph_batt` override lifts that gate.

## Documentation

- [`AGENTS.md`](AGENTS.md) — detailed, opinionated project notes: build details,
  architectural boundaries and a long list of hardware-specific gotchas.
- [`docs/io.csv`](docs/io.csv) — the pin map.
- `docs/*.pdf` — CH585, PAW3395, BMI270, BQ24075 and TPS61222 datasheets.

## Third-party

Vendor and vendored code keeps its own license and style: the WCH SDK
(`StdPeriphDriver/`, `Startup/`, `RVMSIS/`, `ble/`, prebuilt libraries), the
Bosch BMI270/BMI2 API (`src/sensor/`), and the LibDriver SSD1315 core
(`src/display/ssd1315/`, MIT). Only the code outside those trees is maintained
here; the app, transport and driver-port sources are formatted with
`clang-format` (K&R braces, 4-space indent, 100 columns).
