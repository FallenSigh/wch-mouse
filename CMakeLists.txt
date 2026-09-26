# =============================================================================
#  wch-mouse - WCH CH585 (RISC-V) firmware
#
#  Cross-compiled with the MounRiver / WCH RISC-V GCC toolchain.
#  Bare-metal executable: custom linker script, startup assembly, vendor
#  static library (libISP585.a), no host CRT/libc.
#
#  Configure (toolchain is mandatory for a cross build):
#     cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-wch-toolchain.cmake
#     cmake --build build
#
#  Or, with the bundled preset:
#     cmake --preset wch
#     cmake --build --preset wch
#
#  Flashing (needs the external `wchisp` tool):
#     cmake --build build --target flash-usb      # wchisp -u flash wch-mouse.hex
#     cmake --build build --target flash-serial   # wchisp -s flash wch-mouse.hex
#     cmake --build build --target flash-mrs      # MounRiver CommunicationLib (WCH-Link ISP)
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

# -----------------------------------------------------------------------------
#  Project compilation flags (toolchain ISA/ABI flags come from the toolchain
#  file via CMAKE_C_FLAGS_INIT / CMAKE_ASM_FLAGS_INIT).
# -----------------------------------------------------------------------------
set(WCH_PROJECT_FLAGS
    -Os
    -g
    -fmessage-length=0
    -fsigned-char
    -ffunction-sections
    -fdata-sections
    -fno-common
    -fmax-errors=20
    # WCH compiler: put __highcode__ functions into the sections Link.ld expects
    --param=highcode-gen-section-name=1)

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

# -----------------------------------------------------------------------------
#  Sources (kept explicit to match the vendor build exactly; docs/ excluded)
# -----------------------------------------------------------------------------
set(APP_SOURCES
    src/UART.c
    src/ch585_usbhs_device.c
    src/main.c
    src/log.c
    src/bat.c
    src/transport.c
    src/transport_usb.c
    src/motor.c
    src/mouse.c
    src/paw3395.c
    src/paw3395_port.c
    src/bmi270.c
    src/bmi2.c
    src/bmi270_port.c
    src/rgb.c
    src/usb_desc.c)

# Vendor-forked BLE HAL: initialises the radio, configures the 32K clock,
# and registers the HAL task with TMOS. Only compiled when WCH_BLE_ENABLE=ON.
set(BLE_HAL_SOURCES
    ble/HAL/MCU.c
    ble/HAL/RTC.c
    ble/HAL/SLEEP.c)

# Vendor-forked HOGP GATT profile layer (HID 0x1812, Battery 0x180F,
# Device Information 0x180A, Scan Parameters). Forked byte-identical from
# docs/CH585EVT/EVT/EXAM/BLE/HID_Mouse/Profile/ with one substantive
# change: hidmouseservice.c has its HID report map replaced with the
# 6-byte mouse descriptor that matches src/mouse.h's MouseReport_t.
set(BLE_PROFILE_SOURCES
    ble/Profile/hiddev.c
    ble/Profile/hidmouseservice.c
    ble/Profile/battservice.c
    ble/Profile/devinfoservice.c
    ble/Profile/scanparamservice.c
    src/transport_ble.c)

# Vendor-supplied BLE interrupt trampolines. PERI library already provides
# the same symbols; the linker picks one copy. Both define them in
# .highcode.LLE_IRQHandler (functionally identical code paths).
set(BLE_ASM_SOURCES
    ble/LIB/ble_task_scheduler.S)

set(DRIVER_SOURCES
    StdPeriphDriver/CH58x_adc.c
    StdPeriphDriver/CH58x_clk.c
    StdPeriphDriver/CH58x_flash.c
    StdPeriphDriver/CH58x_gpio.c
    StdPeriphDriver/CH58x_i2c.c
    StdPeriphDriver/CH58x_lcd.c
    StdPeriphDriver/CH58x_pwm.c
    StdPeriphDriver/CH58x_pwr.c
    StdPeriphDriver/CH58x_spi0.c
    StdPeriphDriver/CH58x_spi1.c
    StdPeriphDriver/CH58x_sys.c
    StdPeriphDriver/CH58x_timer0.c
    StdPeriphDriver/CH58x_timer1.c
    StdPeriphDriver/CH58x_timer2.c
    StdPeriphDriver/CH58x_timer3.c
    StdPeriphDriver/CH58x_uart0.c
    StdPeriphDriver/CH58x_uart1.c
    StdPeriphDriver/CH58x_uart2.c
    StdPeriphDriver/CH58x_uart3.c
    StdPeriphDriver/CH58x_usbdev.c
    StdPeriphDriver/CH58x_usbhostBase.c
    StdPeriphDriver/CH58x_usbhostClass.c)

set(STARTUP_SOURCES
    Startup/startup_CH585.S)

# Conditional BLE sources (HAL C + Profile GATT services + asm trampolines)
set(_BLE_SOURCES "")
if(WCH_BLE_ENABLE)
  list(APPEND _BLE_SOURCES ${BLE_HAL_SOURCES} ${BLE_PROFILE_SOURCES} ${BLE_ASM_SOURCES})
endif()

add_executable(${PROJECT_NAME}
    ${APP_SOURCES}
    ${DRIVER_SOURCES}
    ${STARTUP_SOURCES}
    ${_BLE_SOURCES})

# -----------------------------------------------------------------------------
#  Include paths  (-I)
# -----------------------------------------------------------------------------
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/RVMSIS)

if(WCH_BLE_ENABLE)
  target_include_directories(${PROJECT_NAME} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/ble/LIB
      ${CMAKE_CURRENT_SOURCE_DIR}/ble/HAL/include
      ${CMAKE_CURRENT_SOURCE_DIR}/ble/Profile
      ${CMAKE_CURRENT_SOURCE_DIR}/ble/Profile/include)
endif()

# -----------------------------------------------------------------------------
#  Defines  (-D)
# -----------------------------------------------------------------------------
# DEBUG selects the printf retarget UART (StdPeriphDriver/CH58x_sys.c):
# 0=UART0, 1=UART1, 2=UART2, 3=UART3. Override with -DWCH_DEBUG_UART=3.
set(WCH_DEBUG_UART 1 CACHE STRING "printf retarget UART: 0=UART0 1=UART1 2=UART2 3=UART3")

# Compile-time log ceiling: LOG_LVL_ERROR / LOG_LVL_WARN / LOG_LVL_INFO / LOG_LVL_DEBUG.
set(WCH_LOG_LEVEL "LOG_LVL_INFO" CACHE STRING "Compile-time log level ceiling")

target_compile_definitions(${PROJECT_NAME} PRIVATE
    DEBUG=${WCH_DEBUG_UART}
    LOG_LEVEL=${WCH_LOG_LEVEL})

if(WCH_BLE_ENABLE)
  target_compile_definitions(${PROJECT_NAME} PRIVATE WCH_BLE_ENABLE)
endif()

if(WCH_DCDC_ENABLE)
  target_compile_definitions(${PROJECT_NAME} PRIVATE WCH_DCDC_ENABLE)
endif()

target_compile_options(${PROJECT_NAME} PRIVATE ${WCH_PROJECT_FLAGS})

# -----------------------------------------------------------------------------
#  Link: custom linker script, no CRT, GC sections, vendor library + libm
# -----------------------------------------------------------------------------
target_link_options(${PROJECT_NAME} PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/Ld/Link.ld
    -nostartfiles
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map,${PROJECT_NAME}.map
    -L${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver
    --specs=nano.specs
    --specs=nosys.specs)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ISP585
    m)

if(WCH_BLE_ENABLE)
  # Vendor-forked peripheral-only BLE library. ~1.1 MB archive; the linker
  # pulls in only the .o files needed to resolve BLE_LibInit / GAPRole_*
  # / tmos_* / ATT_* / GAP_* etc. The full libCH58xBLE.a is left on disk
  # in ble/LIB/ as a fallback if PERI ever fails to resolve something.
  target_link_libraries(${PROJECT_NAME} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/ble/LIB/libCH58xBLE_PERI.a)
endif()

# Firmware image keeps the .elf suffix.
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".elf")

# -----------------------------------------------------------------------------
#  Post-build artifacts: .hex (flash image), .lst (disassembly listing), size
# -----------------------------------------------------------------------------
set(_HEX_FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex")
set(_LST_FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.lst")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex "$<TARGET_FILE:${PROJECT_NAME}>" "${_HEX_FILE}"
    COMMAND ${CMAKE_COMMAND}
            "-DOBJDUMP=${CMAKE_OBJDUMP}"
            "-DINPUT=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DOUTPUT=${_LST_FILE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gen_listing.cmake"
    COMMAND ${CMAKE_SIZE} --format=berkeley "$<TARGET_FILE:${PROJECT_NAME}>"
    COMMENT "Generating ${PROJECT_NAME}.hex / ${PROJECT_NAME}.lst")

# -----------------------------------------------------------------------------
#  Flashing targets (external `wchisp` tool; not part of the default build)
#     cmake --build build --target flash-usb       # wchisp -u flash <hex>
#     cmake --build build --target flash-serial    # wchisp -s flash <hex>
# -----------------------------------------------------------------------------
find_program(WCHISP_EXECUTABLE wchisp)
if(NOT WCHISP_EXECUTABLE)
  set(WCHISP_EXECUTABLE wchisp)
  message(STATUS
    "wchisp not found in PATH - the flash targets will invoke 'wchisp' at build time")
endif()

add_custom_target(flash-usb
    COMMAND ${WCHISP_EXECUTABLE} -u flash "${_HEX_FILE}"
    COMMENT "Flashing ${PROJECT_NAME}.hex over USB (wchisp -u flash)"
    USES_TERMINAL)
add_dependencies(flash-usb ${PROJECT_NAME})

add_custom_target(flash-serial
    COMMAND ${WCHISP_EXECUTABLE} -s flash "${_HEX_FILE}"
    COMMENT "Flashing ${PROJECT_NAME}.hex over serial (wchisp -s flash)"
    USES_TERMINAL)
add_dependencies(flash-serial ${PROJECT_NAME})

# -----------------------------------------------------------------------------
#  MounRiver-method flashing: drives MounRiver Studio's own CommunicationLib
#  (libmcuupdate.so) over the WCH-Link ISP path. Unlike openocd this works even
#  when the chip's two-wire debug interface is closed.
#     cmake --build build --target flash-mrs
#  Extra options (e.g. --disable-codeprotect to open the debug interface) can be
#  passed by invoking the script directly:
#     python3 scripts/mrs_flash.py --hex build/wch-mouse.hex --disable-codeprotect
# -----------------------------------------------------------------------------
find_program(PYTHON3_EXECUTABLE NAMES python3 python)

if(PYTHON3_EXECUTABLE)
  add_custom_target(flash-mrs
      COMMAND ${PYTHON3_EXECUTABLE}
              "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mrs_flash.py"
              --hex "${_HEX_FILE}"
      COMMENT "Flashing ${PROJECT_NAME}.hex via MounRiver CommunicationLib (WCH-Link ISP)"
      USES_TERMINAL)
  add_dependencies(flash-mrs ${PROJECT_NAME})
else()
  message(STATUS "python3 not found - the 'flash-mrs' target is unavailable")
endif()
