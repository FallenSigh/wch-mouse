# =============================================================================
#  CMake toolchain file - WCH CH58x / CH585 (RISC-V)
#  MounRiver Studio / WCH "RISC-V Embedded GCC" toolchain.
#
#  Usage (one of):
#     cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-wch-toolchain.cmake
#     cmake --preset wch                     # see CMakePresets.json
#
#  Toolchain discovery order:
#     1. -DWCH_TOOLCHAIN_ROOT=/path (cache) or $WCH_TOOLCHAIN_ROOT (environment)
#     2. the platform-specific default install locations below
#     3. <prefix>gcc on PATH
#
#  Optional cache overrides:
#     -DWCH_TOOLCHAIN_ROOT=/path/to/toolchain   (the directory that holds bin/)
#     -DWCH_TOOLCHAIN_PREFIX=riscv-wch-elf-     (if your GCC uses another prefix)
#
#  Host support: Windows / Linux / macOS. On Windows every tool has a .exe
#  suffix; it is derived from the *host*, because CMAKE_EXECUTABLE_SUFFIX is
#  empty for the bare-metal "Generic" target and would miss the executables.
#
#  This file only describes the *toolchain* (system, compilers, ISA/ABI).
#  Project sources, options and linker script live in CMakeLists.txt.
# =============================================================================
cmake_minimum_required(VERSION 3.21)

# --- Target system -----------------------------------------------------------
set(CMAKE_SYSTEM_NAME      Generic)   # bare-metal, no OS
set(CMAKE_SYSTEM_PROCESSOR riscv)

# Host executable suffix (see header note).
if(CMAKE_HOST_WIN32)
  set(_WCH_EXE_SUFFIX ".exe")
else()
  set(_WCH_EXE_SUFFIX "")
endif()

# --- Toolchain program prefix ------------------------------------------------
set(WCH_TOOLCHAIN_PREFIX "riscv32-wch-elf-"
    CACHE STRING "WCH GCC program name prefix")

# Host tools must be searched on the host, never inside the target sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# --- Root discovery ----------------------------------------------------------
# Accept either the toolchain root (contains bin/) or the bin/ directory itself.
function(_wch_normalize_root in out)
  if(EXISTS "${in}/bin/${WCH_TOOLCHAIN_PREFIX}gcc${_WCH_EXE_SUFFIX}")
    set(${out} "${in}" PARENT_SCOPE)
  elseif(EXISTS "${in}/${WCH_TOOLCHAIN_PREFIX}gcc${_WCH_EXE_SUFFIX}")
    get_filename_component(_parent "${in}" DIRECTORY)
    set(${out} "${_parent}" PARENT_SCOPE)
  else()
    set(${out} "" PARENT_SCOPE)
  endif()
endfunction()

# A user-supplied root wins; normalize it (Windows users often pass "\" paths).
if(WCH_TOOLCHAIN_ROOT)
  file(TO_CMAKE_PATH "${WCH_TOOLCHAIN_ROOT}" WCH_TOOLCHAIN_ROOT)
elseif(DEFINED ENV{WCH_TOOLCHAIN_ROOT})
  file(TO_CMAKE_PATH "$ENV{WCH_TOOLCHAIN_ROOT}" WCH_TOOLCHAIN_ROOT)
endif()

if(WCH_TOOLCHAIN_ROOT)
  _wch_normalize_root("${WCH_TOOLCHAIN_ROOT}" _WCH_NORMALIZED)
  if(NOT _WCH_NORMALIZED)
    # Do not hard-fail: a stale preset pointing at another OS's path should
    # still let the default search below find the local toolchain.
    message(STATUS
      "WCH_TOOLCHAIN_ROOT='${WCH_TOOLCHAIN_ROOT}' has no "
      "'${WCH_TOOLCHAIN_PREFIX}gcc${_WCH_EXE_SUFFIX}'; searching default locations")
    # Drop both scopes so the search below actually runs (a -D/preset value
    # lives in the cache, which a plain unset() would leave behind).
    unset(WCH_TOOLCHAIN_ROOT)
    unset(WCH_TOOLCHAIN_ROOT CACHE)
  else()
    set(WCH_TOOLCHAIN_ROOT "${_WCH_NORMALIZED}")
  endif()
endif()

# Platform-specific default locations.
if(NOT WCH_TOOLCHAIN_ROOT)
  if(CMAKE_HOST_WIN32)
    set(_WCH_SEARCH_ROOTS
        "$ENV{ProgramFiles}/RISC-V-Embedded-GCC15"
        "$ENV{ProgramFiles}/WCH/RISC-V-Embedded-GCC15"
        "$ENV{ProgramFiles}/MounRiver/MounRiver_Studio/toolchain/RISC-V Embedded GCC"
        "C:/Program Files (x86)/MounRiver/MounRiver_Studio/toolchain/RISC-V Embedded GCC"
        "$ENV{LOCALAPPDATA}/MounRiver/MounRiver_Studio/toolchain/RISC-V Embedded GCC"
        "$ENV{ProgramData}/RISC-V-Embedded-GCC15"
        "$ENV{USERPROFILE}/MounRiver/MounRiver_Studio/toolchain/RISC-V Embedded GCC"
        "C:/MounRiver/RISC-V-Embedded-GCC15"
        "C:/MounRiver/MounRiver_Studio/toolchain/RISC-V Embedded GCC"
        "C:/ProgramData/RISC-V-Embedded-GCC15"
        "C:/wch/RISC-V-Embedded-GCC15")
    # Catch versioned toolchain folders (e.g. "RISC-V Embedded GCC12/GCC15").
    foreach(_mr
        "$ENV{ProgramFiles}/MounRiver/MounRiver_Studio"
        "C:/Program Files (x86)/MounRiver/MounRiver_Studio"
        "$ENV{LOCALAPPDATA}/MounRiver/MounRiver_Studio"
        "$ENV{ProgramData}/MounRiver/MounRiver_Studio"
        "C:/MounRiver/MounRiver_Studio")
      file(GLOB _mr_tc "${_mr}/toolchain/RISC-V Embedded GCC*")
      list(APPEND _WCH_SEARCH_ROOTS ${_mr_tc})
    endforeach()
  elseif(APPLE)
    set(_WCH_SEARCH_ROOTS
        "/opt/wch/riscv-wch-gcc15"
        "/opt/riscv-wch-elf-gcc15"
        "$ENV{HOME}/MounRiver/MounRiver_Studio/toolchain/RISC-V Embedded GCC"
        "/Applications/MounRiver Studio.app/Contents/Resources/toolchain")
  else()
    set(_WCH_SEARCH_ROOTS
        "/opt/wch/riscv-wch-gcc15"
        "/opt/wch/riscv-wch-gcc"
        "/opt/riscv-wch-elf-gcc15"
        "/opt/riscv-wch-elf-gcc"
        "/opt/MounRiver/MounRiver_Studio/toolchain/RISC-V Embedded GCC"
        "/usr/local/riscv-wch-elf-gcc"
        "$ENV{HOME}/.local/wch/riscv-wch-gcc15"
        "$ENV{HOME}/MounRiver/MounRiver_Studio/toolchain/RISC-V Embedded GCC")
  endif()

  foreach(_candidate ${_WCH_SEARCH_ROOTS})
    _wch_normalize_root("${_candidate}" _WCH_NORMALIZED)
    if(_WCH_NORMALIZED)
      set(WCH_TOOLCHAIN_ROOT "${_WCH_NORMALIZED}")
      break()
    endif()
  endforeach()
endif()

# Last resort: the compiler is already on PATH.
if(NOT WCH_TOOLCHAIN_ROOT)
  find_program(_WCH_GCC_ON_PATH
      NAMES "${WCH_TOOLCHAIN_PREFIX}gcc${_WCH_EXE_SUFFIX}"
            "${WCH_TOOLCHAIN_PREFIX}gcc")
  if(_WCH_GCC_ON_PATH)
    get_filename_component(_WCH_BIN_ON_PATH "${_WCH_GCC_ON_PATH}" DIRECTORY)
    get_filename_component(WCH_TOOLCHAIN_ROOT "${_WCH_BIN_ON_PATH}" DIRECTORY)
  endif()
  unset(_WCH_GCC_ON_PATH CACHE)
endif()

if(NOT WCH_TOOLCHAIN_ROOT)
  message(FATAL_ERROR
    "WCH RISC-V toolchain '${WCH_TOOLCHAIN_PREFIX}gcc${_WCH_EXE_SUFFIX}' not found.\n"
    "  Pass -DWCH_TOOLCHAIN_ROOT=/path/to/toolchain, set the WCH_TOOLCHAIN_ROOT\n"
    "  environment variable, or add the toolchain's bin/ directory to PATH.")
endif()

file(TO_CMAKE_PATH "${WCH_TOOLCHAIN_ROOT}" WCH_TOOLCHAIN_ROOT)
set(WCH_TOOLCHAIN_ROOT "${WCH_TOOLCHAIN_ROOT}"
    CACHE PATH "Root of the WCH RISC-V GCC toolchain" FORCE)

set(_WCH_BIN "${WCH_TOOLCHAIN_ROOT}/bin")

# --- Compilers and binutils --------------------------------------------------
set(CMAKE_C_COMPILER   "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}gcc${_WCH_EXE_SUFFIX}")
set(CMAKE_ASM_COMPILER "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}gcc${_WCH_EXE_SUFFIX}")
set(CMAKE_AR           "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}ar${_WCH_EXE_SUFFIX}")
set(CMAKE_RANLIB       "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}ranlib${_WCH_EXE_SUFFIX}")

# gcc-ar/gcc-ranlib keep LTO objects usable in static archives; prefer them.
if(EXISTS "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}gcc-ar${_WCH_EXE_SUFFIX}")
  set(CMAKE_C_COMPILER_AR     "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}gcc-ar${_WCH_EXE_SUFFIX}")
  set(CMAKE_C_COMPILER_RANLIB "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}gcc-ranlib${_WCH_EXE_SUFFIX}")
endif()

set(CMAKE_OBJCOPY "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}objcopy${_WCH_EXE_SUFFIX}"
    CACHE FILEPATH "objcopy")
set(CMAKE_OBJDUMP "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}objdump${_WCH_EXE_SUFFIX}"
    CACHE FILEPATH "objdump")
set(CMAKE_SIZE    "${_WCH_BIN}/${WCH_TOOLCHAIN_PREFIX}size${_WCH_EXE_SUFFIX}"
    CACHE FILEPATH "size")

if(NOT EXISTS "${CMAKE_C_COMPILER}")
  message(FATAL_ERROR "WCH C compiler not found at '${CMAKE_C_COMPILER}'")
endif()

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
