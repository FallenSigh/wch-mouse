# =============================================================================
#  wch-mouse - WCH CH585 (RISC-V) firmware
#
#  Cross-compiled with the MounRiver / WCH RISC-V GCC toolchain.
#  Bare-metal executables: custom linker script, startup assembly, vendor
#  static library (libISP585.a), no host CRT/libc.
#
#  This tree builds two firmwares:
#     wch-mouse     - the mouse itself (root sources in src/)
#     wch-dongle    - the 2.4G receiver dongle (dongle/, RF host -> USB HID)
#  They share the vendor SDK, HAL, USB HID device layer and RF link config
#  through cmake/wch_sdk.cmake, so a driver or descriptor change reaches both.
#
#  Configure (toolchain is mandatory for a cross build):
#     cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-wch-toolchain.cmake
#     cmake --build build                       # both firmwares
#     cmake --build build --target wch-dongle   # or just one
#
#  Or, with the bundled preset:
#     cmake --preset wch
#     cmake --build --preset wch
#
#  Flashing (needs the external `wchisp` tool). The mouse owns the unsuffixed
#  names; the dongle's are suffixed with -dongle:
#     cmake --build build --target flash-usb          # wchisp -u flash <hex>
#     cmake --build build --target flash-serial       # wchisp -s flash <hex>
#     cmake --build build --target flash-mrs          # MounRiver CommunicationLib
#     cmake --build build --target flash-usb-dongle   # ... same, for the dongle
# =============================================================================
cmake_minimum_required(VERSION 3.21)

if(NOT CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR
    "No toolchain file set. Configure with "
    "-DCMAKE_TOOLCHAIN_FILE=cmake/riscv-wch-toolchain.cmake, or use the 'wch' preset.")
endif()

project(wch-mouse LANGUAGES C ASM)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Emit compile_commands.json for clangd / editor tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Shared vendor sources, HAL, flags, link setup and the wch_firmware* helpers.
include(cmake/wch_sdk.cmake)

# -----------------------------------------------------------------------------
#  WCH closed-source BLE stack (vendor-forked under ble/)
#
#  Default OFF: build is byte-identical to the USB-only firmware, no BLE code
#  or symbols leak in.
#
#  When ON: vendor's peripheral-only BLE library is linked, the HAL sources
#  (MCU.c / RTC.c / SLEEP.c) plus ble_task_scheduler.S are compiled, and
#  src/main.c initialises the stack behind #ifdef WCH_BLE_ENABLE. Phase 1a
#  is build/link integration only - no mouse data over BLE yet.
#
#  Notes:
#  - HAL/MCU.c calls EEPROM_READ / EEPROM_WRITE / GetMACAddress / SYS_GetSysTickCnt;
#    these are already in CH58x_sys.c / ISP585.h. RTC.c calls RTC_InitTime from
#    CH58x_clk.c. No new StdPeriphDriver dependencies.
#  - PERI library already provides strong Ecall_M/U_Mode_Handler and LLE_IRQHandler
#    in .highcode.LLE_IRQHandler. ble_task_scheduler.S defines the same symbols in
#    the same section; the linker keeps the library copy (functionally identical).
#  - Startup/startup_CH585.S declares all BLE-related ISRs as WEAK; the
#    PERI library's strong definitions override the default infinite-loop stubs.
# -----------------------------------------------------------------------------
option(WCH_BLE_ENABLE "Link the closed-source WCH BLE stack (peripheral-only)" OFF)

# CH585 internal DC-DC: brings run current down to roughly 60% of the default
# pass-through LDO. It REQUIRES a 10 uH inductor between VSW and VDCID on the
# board - the datasheet's own wording is "启用前需确认外部硬件电路上存在 DC-DC
# 所需的电感和电容". Set OFF for any board that straps VSW straight to VDCID.
option(WCH_DCDC_ENABLE "Enable the CH585 internal DC-DC (needs the VSW inductor fitted)" ON)

# 2.4G proprietary RF transport (mouse = device/TX, dongle = host/RX). It runs
# on the same vendor radio stack as BLE, so it needs the BLE HAL sources and
# library; the radio is single, so RF and BLE cannot both be live. Enabling
# both options is legal and builds a dual-transport image: src/radio_mode.c
# then picks one at boot (RF preferred), selectable with the BOOT button.
option(WCH_RF_ENABLE "Use the 2.4G RF transport instead of BLE (radio is shared)" OFF)

# -----------------------------------------------------------------------------
#  Mouse application sources, grouped as in src/ (kept explicit to match the
#  vendor build exactly; docs/ excluded). Vendor SDK / HAL / startup come from
#  cmake/wch_sdk.cmake.
# -----------------------------------------------------------------------------
set(APP_SOURCES
    src/main.c
    src/app/radio_mode.c
    src/app/bat.c
    src/app/motor.c
    src/app/mouse.c
    src/display/i2c_bus.c
    src/display/ssd1315_port.c
    src/display/ssd1315/driver_ssd1315.c
    src/transport/transport.c
    src/transport/transport_usb.c
    src/transport/transport_ble.c
    src/transport/transport_rf.c
    src/sensor/paw3395.c
    src/sensor/paw3395_port.c
    src/sensor/bmi270.c
    src/sensor/bmi2.c
    src/sensor/bmi270_port.c
    src/led/rgb.c
    src/led/rgb_fx.c)

add_executable(wch-mouse ${APP_SOURCES})

wch_firmware(wch-mouse "")

if(WCH_DCDC_ENABLE)
  target_compile_definitions(wch-mouse PRIVATE WCH_DCDC_ENABLE)
endif()

# transport_ble.c and transport_rf.c are always compiled and compile to an
# empty stub unless their define selects them, so at most one radio backend is
# ever live. Both options on builds both and lets src/radio_mode.c choose at
# boot; USB-only (neither option) leaves the radio stack out entirely.
if(WCH_BLE_ENABLE AND WCH_RF_ENABLE)
  wch_firmware_radio(wch-mouse BOTH)
elseif(WCH_BLE_ENABLE)
  wch_firmware_radio(wch-mouse BLE)
elseif(WCH_RF_ENABLE)
  wch_firmware_radio(wch-mouse RF)
endif()

# -----------------------------------------------------------------------------
#  2.4G receiver dongle - same toolchain, SDK and USB HID device layer.
# -----------------------------------------------------------------------------
add_subdirectory(dongle)
