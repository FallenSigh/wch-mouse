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
#  Sources (kept explicit to match the vendor build exactly; docs/ excluded)
# -----------------------------------------------------------------------------
set(APP_SOURCES
    src/UART.c
    src/ch585_usbhs_device.c
    src/main.c
    src/motor.c
    src/mouse.c
    src/paw3395.c
    src/bmi270.c
    src/bmi2.c
    src/bmi270_port.c
    src/rgb.c
    src/usb_desc.c)

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

add_executable(${PROJECT_NAME}
    ${APP_SOURCES}
    ${DRIVER_SOURCES}
    ${STARTUP_SOURCES})

# -----------------------------------------------------------------------------
#  Include paths  (-I)
# -----------------------------------------------------------------------------
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/StdPeriphDriver/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/RVMSIS)

# -----------------------------------------------------------------------------
#  Defines  (-D)
# -----------------------------------------------------------------------------
# DEBUG selects the printf retarget UART (StdPeriphDriver/CH58x_sys.c):
# 0=UART0, 1=UART1, 2=UART2, 3=UART3. Override with -DWCH_DEBUG_UART=3.
set(WCH_DEBUG_UART 1 CACHE STRING "printf retarget UART: 0=UART0 1=UART1 2=UART2 3=UART3")

target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG=${WCH_DEBUG_UART})

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
