#!/usr/bin/env python3
"""Talk to the wch-mouse configuration channel over its HID Feature report.

The mouse exposes a vendor-defined Feature report (usage page 0xFF00, Report
ID 0x02, 63 data bytes) on its HID interface. HID gives the host no payload
on a GET_FEATURE, so every transaction is two steps:

    SET_FEATURE  <- host writes a request frame
    GET_FEATURE  -> host reads the answer the device prepared

Frame (little-endian, 63 bytes):

    [0]     cmd                 (bit 7 set on a response)
    [1]     seq                 (echoed by the device)
    [2]     len                 (payload bytes)
    [3..]   payload
    [3+len] sum8                (sum of bytes 0..2+len)
    rest    0

A reply's payload starts with a status byte (0 = OK).

Requires the ``hid`` module:  pip install hidapi
"""

from __future__ import annotations

import argparse
import sys
import time

# USB ids and the report shape, matching src/bsp/usb_desc.h and src/app/proto.h.
VID = 0x1A86
PID = 0xFE0C
HID_ITF = 2
# The vendor Feature lives in its own top-level collection (usage page 0xFF00)
# on interface 2; Windows exposes it as a separate HID collection from the
# mouse collection, so select it by usage page, not just by interface number.
USAGE_PAGE = 0xFF00
REPORT_ID = 0x02
FRAME_LEN = 63

# Commands (src/app/proto.c).
CMD_GET_VERSION = 0x01
CMD_GET_INFO = 0x02
CMD_GET_RADIO = 0x03
CMD_GET_DPI = 0x10
CMD_SET_DPI = 0x11
CMD_GET_SENSOR = 0x12
CMD_SET_SENSOR = 0x13
CMD_GET_LIFT = 0x14
CMD_SET_LIFT = 0x15
CMD_GET_AIR_SENS = 0x16
CMD_SET_AIR_SENS = 0x17
CMD_GET_AIR_ODR = 0x18
CMD_SET_AIR_ODR = 0x19
CMD_GET_BATTERY = 0x24
CMD_GET_RGB = 0x30
CMD_SET_RGB = 0x31
CMD_GET_OLED = 0x40
CMD_SET_OLED = 0x41
CMD_GET_RATE = 0x42
CMD_SET_RATE = 0x43
CMD_BIO_ACQ = 0x50
CMD_BIO_SLEEP = 0x51
CMD_GET_PERIPH = 0x60
CMD_SET_PERIPH = 0x61
CMD_GET_MOTOR = 0x62
CMD_SET_MOTOR = 0x63
CMD_GET_MOTOR_EN = 0x64
CMD_SET_MOTOR_EN = 0x65

STATUS = {
    0x00: "OK",
    0x01: "unknown command",
    0x02: "bad length",
    0x03: "bad checksum",
    0x04: "unsupported",
}

SENSOR_MODES = ("high-performance", "low-power", "office", "corded-gaming")
LIFT_CUTS = ("1mm", "2mm")
RATES = (125, 250, 500, 1000, 2000, 4000, 8000)
# Air-mouse presets, mirroring the tables in src/app/air_mouse.c.
AIR_SENS = ((9, 8), (18, 16), (27, 24), (36, 32), (54, 48), (72, 64), (108, 96))
AIR_ODR_HZ = (25, 50, 100, 200, 400, 800, 1600, 3200)
# The rate command's first payload byte selects the link (src/app/proto.c).
LINKS = {"usb": 0, "rf": 1, "ble": 2}
PID_DONGLE = 0xFE0D
# Set from --no-reply: send and do not wait for a response.
SEND_ONLY = False
# How long to wait for the reply. Through the dongle a command has to cross the
# RF link, run, and have its answer streamed back, so the window has to cover the
# slowest thing the mouse can be asked to do: bringing the panel up NACKs for
# ~120 ms while its charge pump settles, BIO acquisition adds ~50 ms, and the
# answer itself is 8 x 8 passes. 400 ms covers all of it.
REPLY_WAIT = 0.03
REPLY_WAIT_RF = 0.4


_seq = 0


def next_seq() -> int:
    """A fresh sequence per request. The reply echoes it, which is how a read-back
    is matched to the request that caused it: over RF the answer streams back on
    its own schedule, so a stale one can be sitting there when we look."""
    global _seq
    _seq = (_seq + 1) & 0xFF
    return _seq


def build_frame(cmd: int, seq: int, payload: bytes = b"") -> bytes:
    """Encode one request frame (FRAME_LEN bytes; transact adds the Report ID)."""
    if len(payload) > FRAME_LEN - 4:
        raise ValueError(f"payload too long ({len(payload)} > {FRAME_LEN - 4})")

    frame = bytearray(FRAME_LEN)
    frame[0] = cmd & 0xFF
    frame[1] = seq & 0xFF
    frame[2] = len(payload)
    frame[3:3 + len(payload)] = payload
    frame[3 + len(payload)] = sum(frame[0:3 + len(payload)]) & 0xFF
    return bytes(frame)


def ok(status: int) -> None:
    if status != 0x00:
        raise SystemExit(f"device returned status {status:#04x} ({STATUS.get(status, '?')})")


def open_device(opts):
    try:
        import hid
    except ImportError:
        raise SystemExit("the 'hid' module is missing: pip install hidapi")

    infos = hid.enumerate(opts.vid, opts.pid)
    if opts.list:
        for info in infos:
            print(f"itf={info.get('interface_number')} "
                  f"usage={info.get('usage_page'):#06x}:{info.get('usage'):#06x} "
                  f"{info.get('product_string')} "
                  f"{info['path'].decode(errors='replace')}")
        raise SystemExit(0)

    if not infos:
        raise SystemExit(f"no HID device {opts.vid:#06x}:{opts.pid:#06x} found "
                         "(plugged in? new descriptor flashed? re-plug needed?)")

    want = opts.path.encode() if opts.path else None
    chosen = next((i for i in infos if want and i["path"] == want), None)
    if chosen is None:
        chosen = next((i for i in infos if i.get("usage_page") == USAGE_PAGE), None)
    if chosen is None:
        chosen = next((i for i in infos if i.get("interface_number") == HID_ITF), infos[0])

    path = chosen["path"]
    print(f"target {path.decode(errors='replace')} itf={chosen.get('interface_number')} "
          f"usage={chosen.get('usage_page')}:{chosen.get('usage')}")
    try:
        if hasattr(hid, "Device"):      # pyhidapi / hidapi: Device(path=...)
            return hid.Device(path=path)

        dev = hid.device()              # older pyhidapi: device() + open_path()
        dev.open_path(path)
        return dev
    except Exception as exc:
        raise SystemExit(
            f"cannot open {path.decode(errors='replace')}: {exc}\n"
            "hidraw needs read/write access. Either run this as root, or install the\n"
            "udev rule shipped in this repo:\n"
            "  sudo cp scripts/99-wch-mouse.rules /etc/udev/rules.d/\n"
            "  sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=hidraw"
        )


def transact(dev, cmd: int, payload: bytes = b"", seq=None, wait=None):
    """Send one request and return (status, data) from the reply."""
    if seq is None:
        seq = next_seq()

    sent = dev.send_feature_report(bytes([REPORT_ID]) + build_frame(cmd, seq, payload))
    if sent is not None and sent < 0:
        raise SystemExit(f"the device rejected the feature report ({sent})")

    if SEND_ONLY:
        print(f"sent cmd {cmd:#04x} seq {seq}, {len(payload)} payload bytes, no reply requested")
        return 0x00, bytes(8)

    deadline = time.monotonic() + (REPLY_WAIT if wait is None else wait)
    want_cmd = (cmd | 0x80) & 0xFF
    want_seq = seq & 0xFF
    last     = b""

    while True:
        rsp = bytes(dev.get_feature_report(REPORT_ID, 1 + FRAME_LEN))
        if len(rsp) < 1 + 4:
            raise SystemExit(f"short response ({len(rsp)} bytes)")

        reply = rsp[1:1 + FRAME_LEN]  # strip the Report ID prefix
        last  = reply

        if reply[0] == want_cmd and reply[1] == want_seq:
            plen = reply[2]
            return reply[3], reply[4:4 + max(plen - 1, 0)]

        # Through the dongle the answer arrives on the RF link's schedule, so
        # keep looking for ours instead of taking whatever is there.
        if time.monotonic() >= deadline:
            raise SystemExit(
                f"no reply for cmd {cmd:#04x} seq {seq} "
                f"(last was {last[0]:#04x} seq {last[1]})")
        time.sleep(0.005)


def u16(data: bytes, off: int = 0) -> int:
    return data[off] | (data[off + 1] << 8)


def main() -> int:
    ap = argparse.ArgumentParser(description="wch-mouse HID Feature report client")
    ap.add_argument("--vid", type=lambda s: int(s, 0), default=VID)
    ap.add_argument("--pid", type=lambda s: int(s, 0), default=PID)
    ap.add_argument("--path", help="force a HID path (see --list)")
    ap.add_argument("--list", action="store_true", help="list matching HID interfaces and exit")
    ap.add_argument("--seq", type=int, default=None,
                    help="sequence byte to send (default: one per request)")

    sub = ap.add_subparsers(dest="command")
    sub.add_parser("version", help="protocol + firmware version")
    sub.add_parser("info", help="capability bitmap")
    sub.add_parser("radio", help="active radio (rf/ble)")
    sub.add_parser("battery", help="voltage, percent, charge flags")

    p = sub.add_parser("dpi", help="get CPI, or set it with a value")
    p.add_argument("value", nargs="?", type=int, help="CPI to set (50..26000)")
    p = sub.add_parser("sensor", help="get sensor mode, or set 0..3")
    p.add_argument("value", nargs="?", type=int, choices=range(4))
    p = sub.add_parser("lift", help="get lift cut, or set 0=1mm / 1=2mm")
    p.add_argument("value", nargs="?", type=int, choices=(0, 1))
    p = sub.add_parser("air-sens", help="air-mouse sensitivity: get, or set a preset index")
    p.add_argument("value", nargs="?", type=int,
                   help="counts/deg preset index (table in src/app/air_mouse.c)")
    p = sub.add_parser("air-odr", help="air-mouse IMU output rate: get, or set a preset index")
    p.add_argument("value", nargs="?", type=int,
                   help="ODR preset index (table in src/app/air_mouse.c)")
    p = sub.add_parser("rgb", help="get the underglow, or set fields (read-modify-write)")
    p.add_argument("--enable", type=int, choices=(0, 1))
    p.add_argument("--effect", type=int, choices=(0, 1), help="0 = hue cycle, 1 = solid")
    p.add_argument("--brightness", type=int, help="1..255")
    p.add_argument("--color", help="solid colour, RRGGBB")
    p = sub.add_parser("oled", help="get the panel state, or set on/off")
    p.add_argument("value", nargs="?", type=int, choices=(0, 1))
    p = sub.add_parser("rate", help=f"get the report rate in Hz, or set one of {RATES}")
    p.add_argument("value", nargs="?", type=int, help="report rate in Hz (snaps to the nearest)")
    p.add_argument("--link", choices=tuple(LINKS), default="usb",
                   help="which link's rate to get/set (default: usb)")
    p = sub.add_parser("bio-acq", help="BIO acquisition on/off")
    p.add_argument("value", type=int, choices=(0, 1))
    p = sub.add_parser("bio-sleep", help="BIO sleep on/off")
    p.add_argument("value", type=int, choices=(0, 1))
    p = sub.add_parser("periph", help="panel/rail/BIO on battery: get, or set 0/1")
    p.add_argument("value", nargs="?", type=int, choices=(0, 1))
    p = sub.add_parser("motor", help="vibration motor: get, or buzz for N ms (0 = off)")
    p.add_argument("value", nargs="?", type=int)
    p = sub.add_parser("motor-en", help="vibration motor on/off: get, or set 0/1")
    p.add_argument("value", nargs="?", type=int, choices=(0, 1))
    p = sub.add_parser("raw", help="send an arbitrary command with a hex payload")
    p.add_argument("cmd", type=lambda s: int(s, 0))
    p.add_argument("payload", nargs="*", help="payload bytes, e.g. 40 06")

    ap.add_argument("--dongle", action="store_true",
                    help="address the RF dongle instead of the mouse: commands cross the "
                         "RF link, so their replies take longer to come back")
    ap.add_argument("--no-reply", action="store_true",
                    help="send the request without waiting for a response")

    opts = ap.parse_args()

    global SEND_ONLY, REPLY_WAIT
    SEND_ONLY = opts.no_reply
    if opts.dongle:
        opts.pid = PID_DONGLE
        REPLY_WAIT = REPLY_WAIT_RF
    if not opts.list and not opts.command:
        ap.error("a command is required (or use --list)")
    dev = open_device(opts)

    try:
        cmd = opts.command
        if cmd == "version":
            st, d = transact(dev, CMD_GET_VERSION, seq=opts.seq)
            ok(st)
            print(f"protocol {d[0]}  firmware {d[1]}.{d[2]}.{d[3]}")
        elif cmd == "info":
            st, d = transact(dev, CMD_GET_INFO, seq=opts.seq)
            ok(st)
            caps = u16(d)
            print(f"capabilities {caps:#06x}  sensor={caps & 1} radio={(caps >> 1) & 1} "
                  f"battery={(caps >> 2) & 1} bio={(caps >> 3) & 1}")
        elif cmd == "radio":
            st, d = transact(dev, CMD_GET_RADIO, seq=opts.seq)
            ok(st)
            print("radio", "ble" if d[0] else "rf")
        elif cmd == "battery":
            st, d = transact(dev, CMD_GET_BATTERY, seq=opts.seq)
            ok(st)
            print(f"{u16(d)} mV  {d[2]}%  charging={d[3] & 1} "
                  f"power_good={(d[3] >> 1) & 1} fault={(d[3] >> 2) & 1}")
        elif cmd == "dpi":
            value = opts.value
            if value is None:
                st, d = transact(dev, CMD_GET_DPI, seq=opts.seq)
            else:
                st, d = transact(dev, CMD_SET_DPI, value.to_bytes(2, "little"), seq=opts.seq)
            ok(st)
            print(f"CPI {u16(d)}")
        elif cmd == "sensor":
            value = opts.value
            if value is None:
                st, d = transact(dev, CMD_GET_SENSOR, seq=opts.seq)
            else:
                st, d = transact(dev, CMD_SET_SENSOR, bytes([value]), seq=opts.seq)
            ok(st)
            print(f"sensor {d[0]} ({SENSOR_MODES[d[0]]})")
        elif cmd == "lift":
            value = opts.value
            if value is None:
                st, d = transact(dev, CMD_GET_LIFT, seq=opts.seq)
            else:
                st, d = transact(dev, CMD_SET_LIFT, bytes([value]), seq=opts.seq)
            ok(st)
            print(f"lift {d[0]} ({LIFT_CUTS[d[0]]})")
        elif cmd == "air-sens":
            value = opts.value
            if value is None:
                st, d = transact(dev, CMD_GET_AIR_SENS, seq=opts.seq)
            else:
                st, d = transact(dev, CMD_SET_AIR_SENS, bytes([value]), seq=opts.seq)
            ok(st)
            x, y = AIR_SENS[d[0]]
            print(f"air sensitivity idx {d[0]}  {x}/{y} counts/deg")
        elif cmd == "air-odr":
            value = opts.value
            if value is None:
                st, d = transact(dev, CMD_GET_AIR_ODR, seq=opts.seq)
            else:
                st, d = transact(dev, CMD_SET_AIR_ODR, bytes([value]), seq=opts.seq)
            ok(st)
            print(f"air IMU ODR idx {d[0]}  {AIR_ODR_HZ[d[0]]} Hz")
        elif cmd == "rgb":
            st, cur = transact(dev, CMD_GET_RGB, seq=opts.seq)
            ok(st)
            want = list(cur[:6])
            changed = False
            if opts.enable is not None:
                want[0] = opts.enable
                changed = True
            if opts.effect is not None:
                want[1] = opts.effect
                changed = True
            if opts.brightness is not None:
                want[2] = opts.brightness
                changed = True
            if opts.color is not None:
                rgb = bytes.fromhex(opts.color.lstrip("#"))
                if len(rgb) != 3:
                    raise SystemExit("--color must be RRGGBB")
                want[3], want[4], want[5] = rgb[0], rgb[1], rgb[2]
                changed = True
            if changed:
                st, cur = transact(dev, CMD_SET_RGB, bytes(want), seq=opts.seq)
                ok(st)
            print(f"rgb enable={cur[0]} effect={cur[1]} ({'solid' if cur[1] else 'hue'}) "
                  f"brightness={cur[2]} colour={cur[3]:02x}{cur[4]:02x}{cur[5]:02x}")
        elif cmd == "oled":
            if opts.value is None:
                st, d = transact(dev, CMD_GET_OLED, seq=opts.seq)
            else:
                st, d = transact(dev, CMD_SET_OLED, bytes([opts.value]), seq=opts.seq)
            ok(st)
            print("oled", "on" if d[0] else "off")
        elif cmd == "periph":
            if opts.value is None:
                st, d = transact(dev, CMD_GET_PERIPH, seq=opts.seq)
            else:
                st, d = transact(dev, CMD_SET_PERIPH, bytes([opts.value]), seq=opts.seq)
            ok(st)
            print("peripherals on battery", "allowed" if d[0] else "off")
        elif cmd == "motor":
            if opts.value is None:
                st, d = transact(dev, CMD_GET_MOTOR, seq=opts.seq)
            else:
                st, d = transact(dev, CMD_SET_MOTOR,
                                 (opts.value & 0xFFFF).to_bytes(2, "little"), seq=opts.seq)
            ok(st)
            print("motor", "running" if d[0] else "off")
        elif cmd == "motor-en":
            if opts.value is None:
                st, d = transact(dev, CMD_GET_MOTOR_EN, seq=opts.seq)
            else:
                st, d = transact(dev, CMD_SET_MOTOR_EN, bytes([opts.value]), seq=opts.seq)
            ok(st)
            print("motor", "enabled" if d[0] else "disabled")
        elif cmd == "rate":
            link = bytes([LINKS[opts.link]])
            if opts.value is None:
                st, d = transact(dev, CMD_GET_RATE, link, seq=opts.seq)
            else:
                st, d = transact(dev, CMD_SET_RATE, link + opts.value.to_bytes(2, "little"),
                                 seq=opts.seq)
            ok(st)
            print(f"report rate {u16(d)} Hz ({opts.link})")
        elif cmd == "bio-acq":
            st, _ = transact(dev, CMD_BIO_ACQ, bytes([opts.value]), seq=opts.seq)
            ok(st)
            print("bio acquisition", "on" if opts.value else "off")
        elif cmd == "bio-sleep":
            st, _ = transact(dev, CMD_BIO_SLEEP, bytes([opts.value]), seq=opts.seq)
            ok(st)
            print("bio sleep", "on" if opts.value else "off")
        elif cmd == "raw":
            payload = bytes(int(b, 0) for b in opts.payload)
            st, d = transact(dev, opts.cmd, payload, seq=opts.seq)
            print(f"status {st:#04x} ({STATUS.get(st, '?')})  data {d.hex(' ')}")
    finally:
        dev.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
