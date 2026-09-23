# =============================================================================
#  CMake toolchain file - WCH CH58x / CH585 (RISC-V)
#  MounRiver Studio / WCH GCC toolchain
#
#  Usage (one of):
#     cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-wch-toolchain.cmake
#     cmake --preset wch                     # see CMakePresets.json
#
#  Optional cache overrides:
#     -DWCH_TOOLCHAIN_ROOT=/path/to/riscv-wch-gcc15
#     -DWCH_TOOLCHAIN_PREFIX=riscv32-wch-elf-
#
#  This file only describes the *toolchain* (system, compilers, ISA/ABI).
#  Project sources, options and linker script live in CMakeLists.txt.
# =============================================================================
cmake_minimum_required(VERSION 3.21)

# --- Target system -----------------------------------------------------------
set(CMAKE_SYSTEM_NAME      Generic)   # bare-metal, no OS
set(CMAKE_SYSTEM_PROCESSOR riscv)

# --- Toolchain discovery -----------------------------------------------------
set(WCH_TOOLCHAIN_PREFIX "riscv32-wch-elf-"
    CACHE STRING "WCH GCC program name prefix")

if(NOT WCH_TOOLCHAIN_ROOT)
  if(DEFINED ENV{WCH_TOOLCHAIN_ROOT})
    set(WCH_TOOLCHAIN_ROOT "$ENV{WCH_TOOLCHAIN_ROOT}")
  else()
    foreach(_candidate
            "/opt/wch/riscv-wch-gcc15"
            "/opt/wch/riscv-wch-gcc"
            "$ENV{HOME}/MounRiver/MounRiver_Studio/toolchain/RISC-V Embedded GCC"
            "C:/MounRiver/MounRiver_Studio/toolchain/RISC-V Embedded GCC")
      if(EXISTS "${_candidate}/bin/${WCH_TOOLCHAIN_PREFIX}gcc")
        set(WCH_TOOLCHAIN_ROOT "${_candidate}")
        break()
      endif()
    endforeach()
  endif()
endif()

if(NOT WCH_TOOLCHAIN_ROOT)
  message(FATAL_ERROR
    "WCH RISC-V toolchain not found. Pass -DWCH_TOOLCHAIN_ROOT=/path/to/toolchain")
endif()
set(WCH_TOOLCHAIN_ROOT "${WCH_TOOLCHAIN_ROOT}"
    CACHE PATH "Root of the WCH RISC-V GCC toolchain" FORCE)

set(_WCH_BIN "${WCH_TOOLCHAIN_ROOT}/bin")

# --- Compilers and binutils --------------------------------------------------
set(CMAKE_C_COMPILER   "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_ASM_COMPILER "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_AR           "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}ar")
set(CMAKE_RANLIB       "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}ranlib")
set(CMAKE_OBJCOPY "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}objcopy" CACHE FILEPATH "objcopy")
set(CMAKE_OBJDUMP "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}objdump" CACHE FILEPATH "objdump")
set(CMAKE_SIZE    "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}size"    CACHE FILEPATH "size")

# Bare-metal: compiler checks must not try to link/run a host executable.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# --- Limit find_* to the toolchain -------------------------------------------
set(CMAKE_FIND_ROOT_PATH "${WCH_TOOLCHAIN_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# --- Mandatory ISA / ABI (CH585: RV32IMC + Zba/Zbb/Zbc/Zbs + XW) -------------
# NOTE: kept as a single space-separated string (not a CMake list) so it can be
# assigned directly to CMAKE_<LANG>_FLAGS_INIT.
set(WCH_ARCH_FLAGS
    "-march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore")

set(CMAKE_C_FLAGS_INIT   "${WCH_ARCH_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${WCH_ARCH_FLAGS}")
