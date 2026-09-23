#!/usr/bin/env python3
"""Flash a WCH CH58x/CH585 target using MounRiver Studio's own flashing library.

This drives the exact same native library MounRiver Studio uses for its
"Download" button -- ``libmcuupdate.so`` from its ``CommunicationLib``
component. That library talks to the WCH-Link over the ISP/bootloader path
(the same SWCLK/SWDIO wires), so it flashes successfully even when the chip's
two-wire *debug* interface is closed -- the case where openocd reports
"WCH-Link failed to connect with riscvchip".

It deliberately mirrors the call sequence extracted from MRS's debug extension:

    McuCompiler_SetTargetChip(chipId, wire_mode)   # wire_mode: 1 = 2-wire, 0 = 1-wire
    MRSFunc_FlashOperationEx(chipId, speed, flags, address, hex_path)

``flags`` is the same bit mask MRS builds from its Download Configuration:

    reset            1
    verify           2
    program          4
    erase            8
    sdiPrintf       16
    disableCodeprot 32
    clearCodeFlash  64
    disablePowerOut 128

Exit status: 0 on success, non-zero on failure (the raw library return code).
"""

from __future__ import annotations

import argparse
import ctypes
import glob
import os
import sys

# --- flags (mirror MRS Download Configuration bit mask) ----------------------
F_RESET = 1
F_VERIFY = 2
F_PROGRAM = 4
F_ERASE = 8
F_SDI_PRINTF = 16
F_DISABLE_CODEPROTECT = 32
F_CLEAR_CODEFLASH = 64
F_DISABLE_POWEROUT = 128

# Chip ids from MounRiver's per-SDK "*-flash.json" (CH585-flash.json: "id": 75).
CHIP_CH584_585 = 75

# MRS return codes worth naming (from its errorCodeMRSFunc2String table).
ERRORS = {
    0: "OK",
    1: "Communication command format error",
    2: "Fail to program or verify",
    3: "Invalid ack cmd. Please check chip status",
    4: "Fail to set start cmd. Please check chip status",
    5: "Code-Protect is currently enabled, or unsupported packaged chip type",
    6: "Link return data error",
    8: "Code-protect currently enabled. Disable Code-Protect first",
    105: "Failed to query Code-Protect status",
    106: "Failed to disable Code-Protect",
    107: "Failed to enable Code-Protect",
    114: "Failed to disable two-line debug interface",
    120: "Before operation, please disable Code-Protect first",
    10001: "Communication failure or chip status error",
}


def find_lib() -> str:
    """Locate libmcuupdate.so inside a MounRiver Studio installation."""
    env = os.environ.get("WCH_COMMUNICATION_LIB_DIR")
    roots = []
    if env:
        roots.append(env)
    for root in (
        "/opt/wch",
        os.path.expanduser("~/apps"),
        os.path.expanduser("~"),
        "/opt",
    ):
        roots.append(root)
    patterns = [
        "**/CommunicationLib/*/libmcuupdate.so",
        "**/components/WCH/Others/CommunicationLib/*/libmcuupdate.so",
    ]
    for root in roots:
        for pat in patterns:
            for hit in sorted(glob.glob(os.path.join(root, pat), recursive=True)):
                if os.path.isfile(hit):
                    return hit
    raise SystemExit(
        "libmcuupdate.so not found. Set WCH_COMMUNICATION_LIB_DIR to the "
        "MounRiver 'CommunicationLib/<version>' directory."
    )


def load_api(lib_path: str):
    lib_dir = os.path.dirname(lib_path)
    # The library dlopens its siblings (libhidapi/libusb/jaylink) and MRS puts
    # this directory on PATH/LD_LIBRARY_PATH, so do the same.
    os.environ["LD_LIBRARY_PATH"] = lib_dir + os.pathsep + os.environ.get("LD_LIBRARY_PATH", "")
    os.environ["PATH"] = lib_dir + os.pathsep + os.environ.get("PATH", "")

    lib = ctypes.CDLL(lib_path)

    def sig(name, argtypes, restype=ctypes.c_int):
        fn = getattr(lib, name)
        fn.argtypes = argtypes
        fn.restype = restype
        return fn

    return {
        "open": sig("McuCompiler_OpenDevice", []),
        "close": sig("McuCompiler_CloseDevice", []),
        "set_target": sig("McuCompiler_SetTargetChip", [ctypes.c_int, ctypes.c_int]),
        "flash": sig(
            "MRSFunc_FlashOperationEx",
            [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p],
        ),
        "get_version": sig(
            "McuCompiler_GetDeviceVersion",
            [ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)],
        ),
        "get_mem_type": sig("MRSFunc_GetMemType", [ctypes.c_int, ctypes.c_int]),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Flash via MounRiver CommunicationLib")
    ap.add_argument("--hex", required=True, help="path to the .hex/.bin firmware image")
    ap.add_argument("--chip", type=int, default=CHIP_CH584_585, help="chip id (CH584/5 = 5)")
    ap.add_argument("--speed", type=int, default=1, choices=(1, 2, 3),
                    help="clock speed: 1=High 2=Middle 3=Low")
    ap.add_argument("--addr", type=lambda s: int(s, 0), default=0,
                    help="flash base address (default 0x0)")
    ap.add_argument("--one-wire", action="store_true", help="use 1-wire debug mode")
    ap.add_argument("--no-erase", action="store_true")
    ap.add_argument("--no-verify", action="store_true")
    ap.add_argument("--no-reset", action="store_true")
    ap.add_argument("--disable-codeprotect", action="store_true",
                    help="also disable flash read-protection (opens the debug interface)")
    ap.add_argument("--lib", help="path to libmcuupdate.so")
    opts = ap.parse_args()

    hex_path = os.path.abspath(opts.hex)
    if not os.path.isfile(hex_path):
        print(f"error: file not found: {hex_path}", file=sys.stderr)
        return 2

    flags = F_PROGRAM
    if not opts.no_erase:
        flags |= F_ERASE
    if not opts.no_verify:
        flags |= F_VERIFY
    if not opts.no_reset:
        flags |= F_RESET
    if opts.disable_codeprotect:
        flags |= F_DISABLE_CODEPROTECT

    lib_path = opts.lib or find_lib()
    api = load_api(lib_path)

    version = ctypes.c_int(0), ctypes.c_int(0)
    ret = api["open"]()
    if ret != 0:
        print(f"error: McuCompiler_OpenDevice() = {ret}", file=sys.stderr)
        return ret
    try:
        v0, v1 = ctypes.c_int(0), ctypes.c_int(0)
        api["get_version"](ctypes.byref(v0), ctypes.byref(v1))
        print(f"WCH-Link opened (device version {v0.value})")
        api["set_target"](opts.chip, 0 if opts.one_wire else 1)
    finally:
        api["close"]()

    print(f"flashing {hex_path}")
    print(f"  chip={opts.chip} speed={opts.speed} flags=0x{flags:x} addr=0x{opts.addr:x}")
    ret = api["flash"](opts.chip, opts.speed, flags, opts.addr, hex_path.encode())

    if ret == 0:
        print("flash: OK")
        return 0
    print(f"flash: FAILED (ret={ret}: {ERRORS.get(ret, 'unknown error')})", file=sys.stderr)
    return ret & 0xFF or 1


if __name__ == "__main__":
    raise SystemExit(main())
