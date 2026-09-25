# AGENTS.md — wch-mouse

Bare-metal firmware for a wireless gaming mouse on the **WCH CH585** (RISC-V RV32IMC + Zba/Zbb/Zbc/Zbs/Xw). There is no host build, no test suite, and no CI — verification is a cross-compile plus on-hardware flashing.

## Build & flash

The WCH/MounRiver GCC toolchain (`riscv32-wch-elf-`) is mandatory; a plain `cmake -B build` fails by design (`CMakeLists.txt` FATAL_ERRORs without a toolchain file).

```sh
cmake --preset wch          # sets toolchain file + binaryDir build/
cmake --build --preset wch  # -> build/wch-mouse.{elf,hex,lst,map}
```

- Toolchain discovery order: `$WCH_TOOLCHAIN_ROOT` -> `/opt/wch/riscv-wch-gcc15` -> `~/MounRiver/...`. Override with `-DWCH_TOOLCHAIN_ROOT=/path`.
- `compile_commands.json` is a **symlink to `build/`** and `.clangd` reads `build/`; run the configure step before expecting LSP/clangd to work on `src/`.
- Flashing uses external tools and is **not** part of the default build:
  - `cmake --build build --target flash-usb` — `wchisp -u flash`
  - `cmake --build build --target flash-serial` — `wchisp -s flash`
  - `cmake --build build --target flash-mrs` — MounRiver `libmcuupdate.so` via `scripts/mrs_flash.py`; works even when the two-wire debug interface is closed. Needs `WCH_COMMUNICATION_LIB_DIR` or auto-discovery.

### CMake cache trap (read before toggling options)

`WCH_BLE_ENABLE` (default **OFF**) and `WCH_DCDC_ENABLE` (default **ON**) are cache variables **not set by the `wch` preset**. Once configured with `-DWCH_BLE_ENABLE=ON`, every later `cmake --preset wch` build silently keeps it ON from `build/CMakeCache.txt`. Pass the flag explicitly or wipe `build/` to restore defaults.

- `WCH_DCDC_ENABLE=ON` requires the 10 µH inductor between VSW and VDCID on the board; set OFF for boards that strap VSW to VDCID.
- Other compile-time knobs: `-DWCH_DEBUG_UART=1` (printf retarget UART 0–3), `-DWCH_LOG_LEVEL=LOG_LVL_DEBUG`.

## Layout & boundaries

- `src/` — application code (edit here).
- `StdPeriphDriver/`, `Startup/`, `RVMSIS/`, `Ld/Link.ld` — vendor SDK. Do not edit; formatting is disabled there to preserve WCH style.
- `ble/` — vendor-forked BLE HAL + GATT profiles + prebuilt closed-source libs (`libCH58xBLE_PERI.a` peripheral-only; full `libCH58xBLE.a` kept as fallback). Only `src/transport_ble.c` is ours.
- `docs/` — datasheets + `io.csv` pin map (tracked). `docs/CH585EVT/` (75 MB vendor SDK) is **gitignored/local-only**; `ble/Profile/` is byte-identical to `docs/CH585EVT/EVT/EXAM/BLE/HID_Mouse/Profile/` except the HID report map in `hidmouseservice.c`.
- New `.c` files must be added to `APP_SOURCES` in `CMakeLists.txt` — source lists are explicit, there is no glob.
- Linker: FLASH 448K @ 0x0, RAM 128K @ 0x20000000; custom `Ld/Link.ld`, no CRT/libc (`-nostartfiles`, `nano.specs` + `nosys.specs`). C99, soft-float.

## Architecture

- Entry point: `src/main.c` bare-metal superloop.
- **Transport router** (`src/transport.c`/`.h`): an array of `transport_t` backends; the first whose `link_up()` is true wins, order USB then BLE (USB has priority). Backends: `transport_usb.c`, `transport_ble.c` (compiled only when `WCH_BLE_ENABLE`). Add new backends to `s_backends[]`.
- **Mouse report**: 6-byte packed `MouseReport_t` (`src/mouse.h`), shared byte-identically by USB and BLE. The layout is duplicated in the USB HID descriptor (`src/usb_desc.c`, `MyMouseReportDesc`) and the BLE report map (`ble/Profile/hidmouseservice.c`) — keep both in sync. Note the stale `4-byte report` comment in `usb_desc.c`: dx/dy are actually 16-bit.
- Sensors: PAW3395 (SPI) in `paw3395.c`; BMI270 via the vendored Bosch BMI2 API (`bmi270.c`/`bmi2.c`). Pin assignments: `docs/io.csv`.
- Logging: `src/log.c` non-blocking ring over UART1; use `LOG_E/W/I/D(tag, ...)`. `LOG_LEVEL` strips higher-verbosity messages at preprocess time. `log_printf` blocks on the UART FIFO — thread context only, never from an ISR.

## Gotchas that will bite

- **TIM0, not TIM3**, drives the 1 kHz tick: the BLE HAL hijacks `TMR3_IRQHandler` when `RF_8K` is defined, which would silently freeze the tick in tri-mode builds.
- `TMOS_SystemProcess()` (625 µs cadence) must be pumped from the main loop; calling it from an ISR corrupts the scheduler.
- USB readiness: `bat_power_good()` only means VBUS is present (a dumb charger passes it), and `USBHS_DevEnumStatus` is never cleared on unplug. `usb_ready()` in `transport_usb.c` clears device state itself when VBUS drops — do not "simplify" it. The USBHS PHY costs 10–20 mA, so `usb_poll()` gates it: on whenever USB owns the link or no BLE link exists, off while BLE is active (with an 800 ms VBUS-edge probe so a newly attached host can still enumerate and take over).
- BLE motion is accumulated in `transport_ble.c`: notifications are paced to the connection interval, and rejected reports must be merged (they carry relative motion) rather than dropped.
- BLE HID only reports over a **bonded, encrypted** link: `HidDev_Report()` gates on `hidDevConnSecure` and the report CCCD is `GATT_PERMIT_ENCRYPT_WRITE`, so the host must pair first. On Linux BlueZ, `bluetoothctl connect` alone is not enough — run `agent on` / `default-agent`, then `pair`, then `trust`. We use `GAPBOND_PAIRING_MODE_INITIATE` and a static device address (`GAP_ConfigDeviceAddr(ADDRTYPE_STATIC, ...)`) so the peripheral drives pairing and the host keeps recognising its bond across reboots.
- Duplicate IRQ symbols are expected: the PERI library provides strong `Ecall_M/U_Mode_Handler` / `LLE_IRQHandler`, while `Startup/startup_CH585.S` declares weak stubs that the library overrides.

## Style

- Format with `clang-format -i <file>` using the repo `.clang-format` (LLVM base; 4-space indent, Attach braces, right-aligned pointers, 100 columns).
