//! Pure, dependency-free wire protocol for the wch-mouse HID Feature-report
//! configuration channel.
//!
//! This module deliberately has **no external dependencies** (no `serde`, no
//! `hidapi`, not even `thiserror`) so it can be compiled and unit-tested in
//! isolation without the Tauri / webkit toolchain. It is a faithful port of
//! `scripts/mouse_proto.py` and `src/app/proto.c`.
//!
//! # Frame layout (little-endian, 63 bytes, no Report ID)
//!
//! ```text
//! [0]     cmd                 (bit 7 set on a response)
//! [1]     seq                 (echoed by the device)
//! [2]     len                 (payload bytes, <= FRAME_LEN - 4)
//! [3..]   payload
//! [3+len] sum8                (sum of bytes 0..2+len)
//! rest    0
//! ```
//!
//! A reply's payload starts with a status byte (0 = OK).

use std::fmt;

/// WCH USB vendor id (dev/prototype ids; see `src/bsp/usb_desc.h`).
pub const VID: u16 = 0x1A86;
/// Product id of the mouse itself (wired, or the wireless device's USB side).
pub const PID_MOUSE: u16 = 0xFE0C;
/// Product id of the 2.4 GHz receiver dongle.
pub const PID_DONGLE: u16 = 0xFE0D;
/// Interface number carrying the vendor Feature collection.
pub const HID_INTERFACE: i32 = 2;
/// Usage page of the vendor Feature report.
pub const USAGE_PAGE: u16 = 0xFF00;
/// Feature report id (the vendor Feature collection's Report ID).
pub const REPORT_ID: u8 = 0x02;
/// Total frame length in bytes, no Report ID.
pub const FRAME_LEN: usize = 63;
/// Maximum payload a frame can carry (`len` byte included).
pub const MAX_PAYLOAD: usize = FRAME_LEN - 4;
/// Reply timeout for the wired (USB) path, in milliseconds.
pub const REPLY_WAIT_MS: u64 = 30;
/// Reply timeout for the dongle (RF) path, in milliseconds. A command has to
/// cross the RF link, run, and have its answer streamed back; the panel
/// bring-up alone NACKs for ~120 ms, so the window has to be generous.
pub const REPLY_WAIT_RF_MS: u64 = 400;
/// How often the transact poll loop re-reads the Feature report.
pub const POLL_INTERVAL_MS: u64 = 5;

/// Supported report rates in Hz (`src/app/settings.c`).
pub const RATES: [u16; 7] = [125, 250, 500, 1000, 2000, 4000, 8000];
/// Air-mouse sensitivity presets as (x, y) counts/deg (`src/app/air_mouse.c`).
pub const AIR_SENS: [(u16, u16); 7] = [
    (9, 8),
    (18, 16),
    (27, 24),
    (36, 32),
    (54, 48),
    (72, 64),
    (108, 96),
];
/// Air-mouse IMU output data rates in Hz (`src/app/air_mouse.c`).
pub const AIR_ODR_HZ: [u16; 8] = [25, 50, 100, 200, 400, 800, 1600, 3200];
/// Optical sensor power modes (`paw3395.h`).
pub const SENSOR_MODES: [&str; 4] = ["high-performance", "low-power", "office", "corded-gaming"];
/// Lift-off cut heights (`paw3395.h`).
pub const LIFT_CUTS: [&str; 2] = ["1mm", "2mm"];

/// Command codes (`src/app/proto.c`). Bit 7 marks a reply.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum Command {
    GetVersion = 0x01,
    GetInfo = 0x02,
    GetRadio = 0x03,
    GetDpi = 0x10,
    SetDpi = 0x11,
    GetSensor = 0x12,
    SetSensor = 0x13,
    GetLift = 0x14,
    SetLift = 0x15,
    GetAirSens = 0x16,
    SetAirSens = 0x17,
    GetAirOdr = 0x18,
    SetAirOdr = 0x19,
    GetBattery = 0x24,
    GetRgb = 0x30,
    SetRgb = 0x31,
    GetOled = 0x40,
    SetOled = 0x41,
    GetRate = 0x42,
    SetRate = 0x43,
    BioAcq = 0x50,
    BioSleep = 0x51,
    GetPeriph = 0x60,
    SetPeriph = 0x61,
    GetMotor = 0x62,
    SetMotor = 0x63,
    GetMotorEn = 0x64,
    SetMotorEn = 0x65,
}

impl Command {
    /// The raw command byte.
    pub fn code(self) -> u8 {
        self as u8
    }

    /// The matching reply byte (`cmd | 0x80`).
    pub fn response_code(self) -> u8 {
        self.code() | 0x80
    }

    /// Decode a raw byte, or `None` if it is not a known request command.
    pub fn from_code(code: u8) -> Option<Self> {
        Some(match code {
            0x01 => Command::GetVersion,
            0x02 => Command::GetInfo,
            0x03 => Command::GetRadio,
            0x10 => Command::GetDpi,
            0x11 => Command::SetDpi,
            0x12 => Command::GetSensor,
            0x13 => Command::SetSensor,
            0x14 => Command::GetLift,
            0x15 => Command::SetLift,
            0x16 => Command::GetAirSens,
            0x17 => Command::SetAirSens,
            0x18 => Command::GetAirOdr,
            0x19 => Command::SetAirOdr,
            0x24 => Command::GetBattery,
            0x30 => Command::GetRgb,
            0x31 => Command::SetRgb,
            0x40 => Command::GetOled,
            0x41 => Command::SetOled,
            0x42 => Command::GetRate,
            0x43 => Command::SetRate,
            0x50 => Command::BioAcq,
            0x51 => Command::BioSleep,
            0x60 => Command::GetPeriph,
            0x61 => Command::SetPeriph,
            0x62 => Command::GetMotor,
            0x63 => Command::SetMotor,
            0x64 => Command::GetMotorEn,
            0x65 => Command::SetMotorEn,
            _ => return None,
        })
    }
}

/// Reply status byte, first payload byte of every reply (`src/app/proto.c`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum Status {
    Ok = 0x00,
    UnknownCommand = 0x01,
    BadLength = 0x02,
    BadChecksum = 0x03,
    Unsupported = 0x04,
}

impl Status {
    /// Decode a raw status byte, or `None` if it is not one we know.
    pub fn from_code(code: u8) -> Option<Self> {
        Some(match code {
            0x00 => Status::Ok,
            0x01 => Status::UnknownCommand,
            0x02 => Status::BadLength,
            0x03 => Status::BadChecksum,
            0x04 => Status::Unsupported,
            _ => return None,
        })
    }

    /// Human-readable label, matching `scripts/mouse_proto.py`.
    pub fn label(self) -> &'static str {
        match self {
            Status::Ok => "OK",
            Status::UnknownCommand => "unknown command",
            Status::BadLength => "bad length",
            Status::BadChecksum => "bad checksum",
            Status::Unsupported => "unsupported",
        }
    }

    /// True when the device accepted the request.
    pub fn is_ok(self) -> bool {
        matches!(self, Status::Ok)
    }
}

impl fmt::Display for Status {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} ({:#04x})", self.label(), *self as u8)
    }
}

/// Which link a report rate applies to (`PROTO_CMD_GET/SET_RATE`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum Link {
    Usb = 0,
    Rf = 1,
    Ble = 2,
}

impl Link {
    /// The raw link-selector byte.
    pub fn code(self) -> u8 {
        self as u8
    }

    /// Decode a raw selector byte.
    pub fn from_code(code: u8) -> Option<Self> {
        Some(match code {
            0 => Link::Usb,
            1 => Link::Rf,
            2 => Link::Ble,
            _ => return None,
        })
    }

    /// Short name used by the host tooling.
    pub fn label(self) -> &'static str {
        match self {
            Link::Usb => "usb",
            Link::Rf => "rf",
            Link::Ble => "ble",
        }
    }

    /// The reply wait this link implies, in milliseconds. Only the wired path
    /// and the dongle exist today; the dongle (RF) is the slow one.
    pub fn timeout_ms(self) -> u64 {
        match self {
            Link::Usb => REPLY_WAIT_MS,
            Link::Rf | Link::Ble => REPLY_WAIT_RF_MS,
        }
    }
}

/// Errors that can occur encoding or decoding a frame. Kept local so the
/// protocol module needs nothing but `std`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ProtocolError {
    /// Payload does not fit in the frame.
    PayloadTooLong(usize),
    /// The reply frame is shorter than the header.
    ShortFrame(usize),
    /// The length byte or the frame buffer is inconsistent.
    BadLength,
    /// `sum8` did not match.
    BadChecksum,
    /// A status byte outside the known set (or a frame we cannot parse).
    BadReply,
}

impl fmt::Display for ProtocolError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ProtocolError::PayloadTooLong(n) => {
                write!(f, "payload too long ({n} > {MAX_PAYLOAD})")
            }
            ProtocolError::ShortFrame(n) => write!(f, "short frame ({n} bytes)"),
            ProtocolError::BadLength => write!(f, "bad length byte"),
            ProtocolError::BadChecksum => write!(f, "bad checksum"),
            ProtocolError::BadReply => write!(f, "malformed reply"),
        }
    }
}

impl std::error::Error for ProtocolError {}

/// 8-bit wrapping sum, exactly the device's `proto_sum`.
pub fn sum8(bytes: &[u8]) -> u8 {
    bytes.iter().fold(0u8, |acc, b| acc.wrapping_add(*b))
}

/// Little-endian `u16` at `off`, or `None` if it would read past the slice.
pub fn u16_le(bytes: &[u8], off: usize) -> Option<u16> {
    let lo = *bytes.get(off)?;
    let hi = *bytes.get(off + 1)?;
    Some(u16::from_le_bytes([lo, hi]))
}

/// Encode one request frame (63 bytes, no Report ID).
pub fn build_frame(cmd: u8, seq: u8, payload: &[u8]) -> Result<[u8; FRAME_LEN], ProtocolError> {
    if payload.len() > MAX_PAYLOAD {
        return Err(ProtocolError::PayloadTooLong(payload.len()));
    }

    let mut frame = [0u8; FRAME_LEN];
    frame[0] = cmd;
    frame[1] = seq;
    frame[2] = payload.len() as u8;
    frame[3..3 + payload.len()].copy_from_slice(payload);

    let sum_off = 3 + payload.len();
    frame[sum_off] = sum8(&frame[..sum_off]);
    Ok(frame)
}

/// Convenience wrapper around [`build_frame`] taking a [`Command`].
pub fn build_request(
    cmd: Command,
    seq: u8,
    payload: &[u8],
) -> Result<[u8; FRAME_LEN], ProtocolError> {
    build_frame(cmd.code(), seq, payload)
}

/// A decoded reply.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Reply {
    /// Reply command byte (bit 7 set).
    pub cmd: u8,
    /// Sequence byte echoed from the request.
    pub seq: u8,
    /// Payload length as carried in the frame (`data.len() + 1`).
    pub len: u8,
    /// Decoded status byte.
    pub status: Status,
    /// Reply data, excluding the leading status byte.
    pub data: Vec<u8>,
}

impl Reply {
    /// True when this reply answers `cmd` and `seq`.
    pub fn matches(&self, cmd: u8, seq: u8) -> bool {
        self.cmd == (cmd | 0x80) && self.seq == seq
    }

    /// Convenience [`Reply::matches`] for a [`Command`].
    pub fn matches_command(&self, cmd: Command, seq: u8) -> bool {
        self.cmd == cmd.response_code() && self.seq == seq
    }
}

/// Decode a reply frame (Report ID already stripped).
pub fn parse_reply(frame: &[u8]) -> Result<Reply, ProtocolError> {
    if frame.len() < 4 {
        return Err(ProtocolError::ShortFrame(frame.len()));
    }

    let len = frame[2] as usize;
    let sum_off = 3 + len;
    // Need the checksum byte inside the slice.
    if sum_off >= frame.len() {
        return Err(ProtocolError::BadLength);
    }
    if frame[sum_off] != sum8(&frame[..sum_off]) {
        return Err(ProtocolError::BadChecksum);
    }
    // A reply always carries at least the status byte.
    if len == 0 {
        return Err(ProtocolError::BadLength);
    }

    let status = Status::from_code(frame[3]).ok_or(ProtocolError::BadReply)?;
    let data_end = (3 + len).min(frame.len());
    let data = frame[4..data_end].to_vec();

    Ok(Reply {
        cmd: frame[0],
        seq: frame[1],
        len: frame[2],
        status,
        data,
    })
}

/// Strip the leading Report-ID byte from a `get_feature_report` read, keeping
/// at most one frame. Returns `None` for an empty read.
pub fn strip_report_id(rsp: &[u8]) -> Option<&[u8]> {
    if rsp.is_empty() {
        return None;
    }
    Some(&rsp[1..rsp.len().min(1 + FRAME_LEN)])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_set_dpi_frame_vector() {
        // Command 0x11 SET_DPI, seq 1, payload [0x40, 0x06] => CPI 1600.
        let frame = build_frame(0x11, 0x01, &[0x40, 0x06]).expect("frame builds");

        assert_eq!(frame.len(), FRAME_LEN);
        assert_eq!(frame[0], 0x11);
        assert_eq!(frame[1], 0x01);
        assert_eq!(frame[2], 0x02);
        assert_eq!(frame[3], 0x40);
        assert_eq!(frame[4], 0x06);
        // 0x11 + 0x01 + 0x02 + 0x40 + 0x06 = 0x5A
        assert_eq!(frame[5], 0x5A);
        // Everything after the checksum stays zero.
        assert!(frame[6..].iter().all(|b| *b == 0));
    }

    #[test]
    fn get_version_frame_has_sum_of_header_only() {
        let frame = build_request(Command::GetVersion, 0x00, &[]).expect("frame builds");
        assert_eq!(&frame[..4], &[0x01, 0x00, 0x00, 0x01]);
        assert_eq!(frame[3], 0x01);
        assert!(frame[4..].iter().all(|b| *b == 0));
    }

    #[test]
    fn sum8_wraps_and_handles_empty() {
        assert_eq!(sum8(&[]), 0x00);
        assert_eq!(sum8(&[0x01, 0x02, 0x03]), 0x06);
        assert_eq!(sum8(&[0xFF, 0x01]), 0x00);
        assert_eq!(sum8(&[0xFF, 0x02]), 0x01);
    }

    #[test]
    fn payload_too_long_is_rejected() {
        let too_big = [0u8; MAX_PAYLOAD + 1];
        assert_eq!(
            build_frame(0x31, 0x01, &too_big),
            Err(ProtocolError::PayloadTooLong(MAX_PAYLOAD + 1))
        );
    }

    #[test]
    fn parse_set_dpi_reply() {
        // cmd 0x11|0x80 = 0x91, seq 7, len 3, payload = [OK, 0x40, 0x06].
        // sum = 0x91 + 0x07 + 0x03 + 0x00 + 0x40 + 0x06 = 0xE1
        let frame = [0x91u8, 0x07, 0x03, 0x00, 0x40, 0x06, 0xE1, 0x00];

        let reply = parse_reply(&frame).expect("reply parses");
        assert_eq!(reply.cmd, 0x91);
        assert_eq!(reply.seq, 7);
        assert_eq!(reply.len, 3);
        assert_eq!(reply.status, Status::Ok);
        assert_eq!(reply.data, vec![0x40, 0x06]);
        assert!(reply.matches(Command::SetDpi.code(), 7));
        assert!(reply.matches_command(Command::SetDpi, 7));
        assert!(!reply.matches_command(Command::SetDpi, 8));
    }

    #[test]
    fn parse_get_version_reply() {
        // 0x81, seq 3, len 5, payload = [OK, protocol=1, fw 1.0.0]
        // sum = 0x81 + 3 + 5 + 0 + 1 + 1 + 0 + 0 = 0x8B
        let frame = [0x81u8, 0x03, 0x05, 0x00, 0x01, 0x01, 0x00, 0x00, 0x8B];
        let reply = parse_reply(&frame).expect("reply parses");
        assert_eq!(reply.status, Status::Ok);
        assert_eq!(reply.data, vec![0x01, 0x01, 0x00, 0x00]);
        assert_eq!(reply.data[0], 1); // protocol
        assert_eq!(reply.data[1..], [1, 0, 0]); // major.minor.patch
    }

    #[test]
    fn parse_rejects_bad_checksum() {
        let frame = [0x91u8, 0x07, 0x03, 0x00, 0x40, 0x06, 0x00, 0x00];
        assert_eq!(parse_reply(&frame), Err(ProtocolError::BadChecksum));
    }

    #[test]
    fn parse_rejects_short_and_zero_length() {
        assert_eq!(
            parse_reply(&[0x91, 0x01, 0x02]),
            Err(ProtocolError::ShortFrame(3))
        );

        // len = 0 (sum at index 3, no status byte) must not parse as a reply.
        let frame = [0x91u8, 0x01, 0x00, 0x92];
        assert_eq!(parse_reply(&frame), Err(ProtocolError::BadLength));
    }

    #[test]
    fn parse_rejects_unknown_status() {
        // status 0x7F is not a known status code.
        // sum = 0x91 + 0x01 + 0x01 + 0x7F = 0x12
        let frame = [0x91u8, 0x01, 0x01, 0x7F, 0x12];
        assert_eq!(parse_reply(&frame), Err(ProtocolError::BadReply));
    }

    #[test]
    fn strip_report_id_drops_leading_byte() {
        let mut rsp = [0u8; 1 + FRAME_LEN];
        rsp[0] = REPORT_ID;
        rsp[1] = 0x81;
        rsp[2] = 0x2A;

        let frame = strip_report_id(&rsp).expect("a frame");
        assert_eq!(frame.len(), FRAME_LEN);
        assert_eq!(frame[0], 0x81);
        assert_eq!(frame[1], 0x2A);

        assert_eq!(strip_report_id(&[]), None);
    }

    #[test]
    fn command_and_status_roundtrip() {
        let commands = [
            (0x01, Command::GetVersion),
            (0x02, Command::GetInfo),
            (0x03, Command::GetRadio),
            (0x10, Command::GetDpi),
            (0x11, Command::SetDpi),
            (0x12, Command::GetSensor),
            (0x13, Command::SetSensor),
            (0x14, Command::GetLift),
            (0x15, Command::SetLift),
            (0x16, Command::GetAirSens),
            (0x17, Command::SetAirSens),
            (0x18, Command::GetAirOdr),
            (0x19, Command::SetAirOdr),
            (0x24, Command::GetBattery),
            (0x30, Command::GetRgb),
            (0x31, Command::SetRgb),
            (0x40, Command::GetOled),
            (0x41, Command::SetOled),
            (0x42, Command::GetRate),
            (0x43, Command::SetRate),
            (0x50, Command::BioAcq),
            (0x51, Command::BioSleep),
            (0x60, Command::GetPeriph),
            (0x61, Command::SetPeriph),
            (0x62, Command::GetMotor),
            (0x63, Command::SetMotor),
            (0x64, Command::GetMotorEn),
            (0x65, Command::SetMotorEn),
        ];
        for (code, cmd) in commands {
            assert_eq!(Command::from_code(code), Some(cmd));
            assert_eq!(cmd.code(), code);
            assert_eq!(cmd.response_code(), code | 0x80);
        }
        assert_eq!(Command::from_code(0x7F), None);

        assert_eq!(Status::from_code(0x00), Some(Status::Ok));
        assert_eq!(Status::from_code(0x04), Some(Status::Unsupported));
        assert_eq!(Status::from_code(0x05), None);
        assert!(Status::Ok.is_ok());
        assert!(!Status::BadChecksum.is_ok());
        assert_eq!(Status::BadLength.label(), "bad length");
    }

    #[test]
    fn link_codes_and_timeouts() {
        assert_eq!(Link::from_code(0), Some(Link::Usb));
        assert_eq!(Link::from_code(1), Some(Link::Rf));
        assert_eq!(Link::from_code(2), Some(Link::Ble));
        assert_eq!(Link::from_code(3), None);
        assert_eq!(Link::Usb.timeout_ms(), REPLY_WAIT_MS);
        assert_eq!(Link::Rf.timeout_ms(), REPLY_WAIT_RF_MS);
        assert_eq!(Link::Ble.timeout_ms(), REPLY_WAIT_RF_MS);
        assert_eq!(Link::Rf.code(), 1);
        assert_eq!(Link::Ble.label(), "ble");
    }

    #[test]
    fn u16_le_reads_little_endian() {
        assert_eq!(u16_le(&[0x40, 0x06], 0), Some(1600));
        assert_eq!(u16_le(&[0x00, 0x40, 0x06], 1), Some(1600));
        assert_eq!(u16_le(&[0x40], 0), None);
    }

    #[test]
    fn constants_match_firmware_tables() {
        assert_eq!(RATES, [125, 250, 500, 1000, 2000, 4000, 8000]);
        assert_eq!(AIR_SENS.len(), 7);
        assert_eq!(AIR_SENS[6], (108, 96));
        assert_eq!(AIR_ODR_HZ, [25, 50, 100, 200, 400, 800, 1600, 3200]);
        assert_eq!(SENSOR_MODES.len(), 4);
        assert_eq!(LIFT_CUTS, ["1mm", "2mm"]);
    }
}
