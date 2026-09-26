# =============================================================================
#  cmake/wch_sdk.cmake - the half of the build shared by every firmware here.
#
#  The mouse (root CMakeLists.txt) and the 2.4G dongle (dongle/) compile the
#  same vendor SDK, startup script, HAL and prebuilt libraries; only their
#  application sources differ. The lists and helper functions below are the
#  single source of truth for that shared half - add a driver once, both
#  firmwares get it.
#
#  Deliberately plain source lists rather than a CMake library target:
#  - an OBJECT library would force the BLE HAL into the mouse's USB-only build,
#    where the closed-source BLE stack is not linked (unresolved symbols);
#  - a STATIC archive does not reliably pull ble_task_scheduler.S, whose IRQ
#    trampolines the startup references only weakly.
#
#  Include this after project(): it declares cache variables and finds the
#  external flashing tools.
# =============================================================================

# printf retarget UART (StdPeriphDriver/CH58x_sys.c):
# 0=UART0, 1=UART1, 2=UART2, 3=UART3. Override with -DWCH_DEBUG_UART=3.
set(WCH_DEBUG_UART 1 CACHE STRING "printf retarget UART: 0=UART0 1=UART1 2=UART2 3=UART3")

# Compile-time log ceiling: LOG_LVL_ERROR / LOG_LVL_WARN / LOG_LVL_INFO / LOG_LVL_DEBUG.
set(WCH_LOG_LEVEL "LOG_LVL_INFO" CACHE STRING "Compile-time log level ceiling")

# Toolchain ISA/ABI flags come from the toolchain file via CMAKE_C_FLAGS_INIT /
# CMAKE_ASM_FLAGS_INIT.
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
#  Shared sources
# -----------------------------------------------------------------------------
# HID device layer, debug UART and logging. Compiled into both firmwares, so
# the HID report descriptor stays the single source of truth for USB and, via
# src/rf_cfg.h, for the RF link.
set(WCH_SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/src/UART.c
    ${CMAKE_SOURCE_DIR}/src/ch585_usbhs_device.c
    ${CMAKE_SOURCE_DIR}/src/log.c
    ${CMAKE_SOURCE_DIR}/src/usb_desc.c)

set(WCH_DRIVER_SOURCES
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_adc.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_clk.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_flash.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_gpio.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_i2c.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_lcd.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_pwm.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_pwr.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_spi0.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_spi1.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_sys.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_timer0.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_timer1.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_timer2.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_timer3.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_uart0.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_uart1.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_uart2.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_uart3.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_usbdev.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_usbhostBase.c
    ${CMAKE_SOURCE_DIR}/StdPeriphDriver/CH58x_usbhostClass.c)

set(WCH_STARTUP_SOURCES
    ${CMAKE_SOURCE_DIR}/Startup/startup_CH585.S)

# Vendor-forked BLE HAL (radio init, 32K clock, TMOS task) plus the interrupt
# trampolines. The PERI library ships functionally identical strong symbols and
# the linker keeps one copy. Needed whenever the radio is used, BLE or RF.
set(WCH_RADIO_SOURCES
    ${CMAKE_SOURCE_DIR}/ble/HAL/MCU.c
    ${CMAKE_SOURCE_DIR}/ble/HAL/RTC.c
    ${CMAKE_SOURCE_DIR}/ble/HAL/SLEEP.c
    ${CMAKE_SOURCE_DIR}/ble/LIB/ble_task_scheduler.S)

# Vendor-forked HOGP GATT profile layer (HID 0x1812, Battery 0x180F, Device
# Information 0x180A, Scan Parameters). BLE transport only.
set(WCH_BLE_PROFILE_SOURCES
    ${CMAKE_SOURCE_DIR}/ble/Profile/hiddev.c
    ${CMAKE_SOURCE_DIR}/ble/Profile/hidmouseservice.c
    ${CMAKE_SOURCE_DIR}/ble/Profile/battservice.c
    ${CMAKE_SOURCE_DIR}/ble/Profile/devinfoservice.c
    ${CMAKE_SOURCE_DIR}/ble/Profile/scanparamservice.c)

find_program(WCHISP_EXECUTABLE wchisp)
if(NOT WCHISP_EXECUTABLE)
  set(WCHISP_EXECUTABLE wchisp)
  message(STATUS
    "wchisp not found in PATH - the flash targets will invoke 'wchisp' at build time")
endif()

find_program(PYTHON3_EXECUTABLE NAMES python3 python)

# -----------------------------------------------------------------------------
#  wch_firmware(<target> <flash-suffix>)
#
#  Applies to a firmware executable everything it shares with the other one:
#  the vendor sources, include paths, compile settings, link options, the
#  post-build .hex/.lst/.siz artifacts and the flashing targets. Call it right
#  after add_executable().
#
#  <flash-suffix> is appended to the flash target names (flash-usb<suffix>,
#  flash-serial<suffix>, flash-mrs<suffix>) so they stay distinct when several
#  firmwares are built in one tree. The mouse, with no suffix, owns the
#  historical flash-usb / flash-serial / flash-mrs names.
# -----------------------------------------------------------------------------
function(wch_firmware target flash_suffix)
  target_sources(${target} PRIVATE
      ${WCH_SHARED_SOURCES}
      ${WCH_DRIVER_SOURCES}
      ${WCH_STARTUP_SOURCES})

  target_include_directories(${target} PRIVATE
      ${CMAKE_SOURCE_DIR}/src
      ${CMAKE_SOURCE_DIR}/StdPeriphDriver/inc
      ${CMAKE_SOURCE_DIR}/RVMSIS)

  target_compile_definitions(${target} PRIVATE
      DEBUG=${WCH_DEBUG_UART}
      LOG_LEVEL=${WCH_LOG_LEVEL})

  target_compile_options(${target} PRIVATE ${WCH_PROJECT_FLAGS})

  # Custom linker script, no CRT, GC sections, vendor static library + libm.
  target_link_options(${target} PRIVATE
      -T${CMAKE_SOURCE_DIR}/Ld/Link.ld
      -nostartfiles
      -Wl,--gc-sections
      -Wl,--print-memory-usage
      -Wl,-Map,${target}.map
      -L${CMAKE_SOURCE_DIR}/StdPeriphDriver
      --specs=nano.specs
      --specs=nosys.specs)

  target_link_libraries(${target} PRIVATE ISP585 m)

  # Firmware image keeps the .elf suffix.
  set_target_properties(${target} PROPERTIES SUFFIX ".elf")

  set(_hex "${CMAKE_CURRENT_BINARY_DIR}/${target}.hex")
  add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_OBJCOPY} -O ihex "$<TARGET_FILE:${target}>" "${_hex}"
      COMMAND ${CMAKE_COMMAND}
              "-DOBJDUMP=${CMAKE_OBJDUMP}"
              "-DINPUT=$<TARGET_FILE:${target}>"
              "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${target}.lst"
              -P "${CMAKE_SOURCE_DIR}/cmake/gen_listing.cmake"
      COMMAND ${CMAKE_SIZE} --format=berkeley "$<TARGET_FILE:${target}>"
      COMMENT "Generating ${target}.hex / ${target}.lst")

  add_custom_target(flash-usb${flash_suffix}
      COMMAND ${WCHISP_EXECUTABLE} -u flash "${_hex}"
      COMMENT "Flashing ${target}.hex over USB (wchisp -u flash)"
      USES_TERMINAL)
  add_dependencies(flash-usb${flash_suffix} ${target})

  add_custom_target(flash-serial${flash_suffix}
      COMMAND ${WCHISP_EXECUTABLE} -s flash "${_hex}"
      COMMENT "Flashing ${target}.hex over serial (wchisp -s flash)"
      USES_TERMINAL)
  add_dependencies(flash-serial${flash_suffix} ${target})

  # MounRiver method: drives MounRiver Studio's CommunicationLib (libmcuupdate.so)
  # over the WCH-Link ISP path. Works even when the two-wire debug interface is
  # closed. Pass --disable-codeprotect by invoking scripts/mrs_flash.py directly.
  if(PYTHON3_EXECUTABLE)
    add_custom_target(flash-mrs${flash_suffix}
        COMMAND ${PYTHON3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/scripts/mrs_flash.py"
                --hex "${_hex}"
        COMMENT "Flashing ${target}.hex via MounRiver CommunicationLib (WCH-Link ISP)"
        USES_TERMINAL)
    add_dependencies(flash-mrs${flash_suffix} ${target})
  endif()
endfunction()

# -----------------------------------------------------------------------------
#  wch_firmware_radio(<target> <transport>)
#
#  Attaches the vendor radio HAL and library and selects the define the
#  firmware's #ifdefs key off. Skip this entirely for a USB-only build.
#     BLE   - HOGP profile layer + WCH_BLE_ENABLE
#     RF    - WCH_RF_ENABLE (mouse over the 2.4G proprietary link)
#     RADIO - HAL and library only (the dongle, which drives RF itself)
#  BLE and RF share the radio, so never attach both to one firmware.
# -----------------------------------------------------------------------------
function(wch_firmware_radio target transport)
  target_sources(${target} PRIVATE ${WCH_RADIO_SOURCES})
  target_include_directories(${target} PRIVATE
      ${CMAKE_SOURCE_DIR}/ble/LIB
      ${CMAKE_SOURCE_DIR}/ble/HAL/include)

  # Vendor-forked peripheral-only BLE library. ~1.1 MB archive; the linker
  # pulls in only the .o files needed to resolve BLE_LibInit / GAPRole_* /
  # tmos_* / ATT_* / GAP_* etc. The full libCH58xBLE.a is left on disk in
  # ble/LIB/ as a fallback if PERI ever fails to resolve something.
  target_link_libraries(${target} PRIVATE
      ${CMAKE_SOURCE_DIR}/ble/LIB/libCH58xBLE_PERI.a)

  if(transport STREQUAL "BLE")
    target_sources(${target} PRIVATE ${WCH_BLE_PROFILE_SOURCES})
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/ble/Profile
        ${CMAKE_SOURCE_DIR}/ble/Profile/include)
    target_compile_definitions(${target} PRIVATE WCH_BLE_ENABLE)
  elseif(transport STREQUAL "RF")
    target_compile_definitions(${target} PRIVATE WCH_RF_ENABLE)
  endif()
endfunction()
