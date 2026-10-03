//! Error type shared by the HID transport, the device facade and the Tauri
//! commands.

use std::fmt;

use crate::protocol::{ProtocolError, Status};

/// Everything that can go wrong talking to the mouse.
#[derive(Debug)]
pub enum Error {
    /// Lower-level hidapi failure (open/send/read).
    Hid(hidapi::HidError),
    /// Serial-port failure while enumerating or reading the BIO CDC stream.
    Serial(serialport::Error),
    /// Wire-format failure while framing or parsing.
    Protocol(ProtocolError),
    /// The device never produced a reply matching our command/sequence.
    Timeout {
        cmd: u8,
        seq: u8,
        last_cmd: Option<u8>,
        last_seq: Option<u8>,
    },
    /// The device answered with a non-OK status.
    Status(Status),
    /// No device is currently connected in the app.
    NotConnected,
    /// Enumeration found no matching interface.
    DeviceNotFound,
    /// A path contained an interior NUL byte.
    Nul(std::ffi::NulError),
    /// The caller passed something the command cannot use.
    InvalidArgument(String),
}

/// Crate-local result alias.
pub type Result<T> = std::result::Result<T, Error>;

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Hid(e) => write!(f, "hid error: {e}"),
            Error::Serial(e) => write!(f, "serial error: {e}"),
            Error::Protocol(e) => write!(f, "protocol error: {e}"),
            Error::Timeout {
                cmd,
                seq,
                last_cmd,
                last_seq,
            } => match (last_cmd, last_seq) {
                (Some(c), Some(s)) => write!(
                    f,
                    "no reply for cmd {cmd:#04x} seq {seq} (last was {c:#04x} seq {s})"
                ),
                _ => write!(f, "no reply for cmd {cmd:#04x} seq {seq}"),
            },
            Error::Status(s) => write!(f, "device returned status {s}"),
            Error::NotConnected => write!(f, "no device connected"),
            Error::DeviceNotFound => write!(f, "no matching HID device found"),
            Error::Nul(e) => write!(f, "invalid device path: {e}"),
            Error::InvalidArgument(m) => write!(f, "{m}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Error::Hid(e) => Some(e),
            Error::Serial(e) => Some(e),
            Error::Protocol(e) => Some(e),
            Error::Nul(e) => Some(e),
            _ => None,
        }
    }
}

impl From<hidapi::HidError> for Error {
    fn from(value: hidapi::HidError) -> Self {
        Error::Hid(value)
    }
}

impl From<serialport::Error> for Error {
    fn from(value: serialport::Error) -> Self {
        Error::Serial(value)
    }
}

impl From<ProtocolError> for Error {
    fn from(value: ProtocolError) -> Self {
        Error::Protocol(value)
    }
}

impl From<std::ffi::NulError> for Error {
    fn from(value: std::ffi::NulError) -> Self {
        Error::Nul(value)
    }
}
