//! Enumeration and the typed device facade: one `Device` owns a transport,
//! remembers the link (which sets the reply timeout) and exposes a method per
//! protocol command.

use std::ffi::CString;
use std::time::Duration;

use hidapi::HidApi;
use serde::{Deserialize, Serialize};

use crate::error::{Error, Result};
use crate::protocol::{
    self, Command, Link, ProtocolError, AIR_ODR_HZ, AIR_SENS, LIFT_CUTS, SENSOR_MODES,
};
use crate::transport::Transport;

/// Which physical product we are talking to.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum DeviceKind {
    /// The mouse itself, over its wired USB interface (short timeout).
    Mouse,
    /// The 2.4 GHz dongle; commands cross the RF link (long timeout).
    Dongle,
}

impl DeviceKind {
    /// Parse the frontend's `pid_kind` argument.
    pub fn parse(s: &str) -> Result<Self> {
        match s.to_ascii_lowercase().as_str() {
            "mouse" => Ok(DeviceKind::Mouse),
            "dongle" => Ok(DeviceKind::Dongle),
            other => Err(Error::InvalidArgument(format!(
                "unknown pid kind {other:?} (expected \"mouse\" or \"dongle\")"
            ))),
        }
    }

    /// USB product id.
    pub fn pid(self) -> u16 {
        match self {
            DeviceKind::Mouse => protocol::PID_MOUSE,
            DeviceKind::Dongle => protocol::PID_DONGLE,
        }
    }

    /// Human label.
    pub fn label(self) -> &'static str {
        match self {
            DeviceKind::Mouse => "mouse",
            DeviceKind::Dongle => "dongle",
        }
    }

    /// The protocol link this product's path uses.
    pub fn link(self) -> Link {
        match self {
            DeviceKind::Mouse => Link::Usb,
            DeviceKind::Dongle => Link::Rf,
        }
    }

    /// Reply wait implied by the link.
    pub fn timeout(self) -> Duration {
        Duration::from_millis(self.link().timeout_ms())
    }
}

/// One enumerated HID interface, as shown in the device picker.
#[derive(Debug, Clone, Serialize)]
pub struct DeviceInfo {
    pub kind: DeviceKind,
    pub path: String,
    pub product: Option<String>,
    pub manufacturer: Option<String>,
    pub serial: Option<String>,
    pub interface_number: i32,
    pub usage_page: u16,
    pub usage: u16,
    /// True for the vendor Feature collection the protocol actually uses.
    pub is_vendor_interface: bool,
}

/// Enumerate every mouse/dongle product, one entry per device.
///
/// hidapi lists a HID interface once per top-level usage: the mouse collection
/// and the vendor Feature collection. Only the vendor Feature collection carries
/// the protocol, so the mouse collection is skipped - connecting to it can only
/// fail. (On Windows each collection has its own path; on Linux they share one
/// `/dev/hidrawN`, so this also collapses the per-usage duplicates.)
pub fn list_devices(api: &HidApi) -> Vec<DeviceInfo> {
    let mut out: Vec<DeviceInfo> = Vec::new();

    for info in api.device_list() {
        if info.vendor_id() != protocol::VID {
            continue;
        }
        let kind = match info.product_id() {
            protocol::PID_MOUSE => DeviceKind::Mouse,
            protocol::PID_DONGLE => DeviceKind::Dongle,
            _ => continue,
        };

        if info.usage_page() != protocol::USAGE_PAGE {
            continue;
        }

        let path = info.path().to_string_lossy().into_owned();
        if out.iter().any(|e| e.path == path) {
            continue;
        }

        out.push(DeviceInfo {
            kind,
            path,
            product: info.product_string().map(str::to_owned),
            manufacturer: info.manufacturer_string().map(str::to_owned),
            serial: info.serial_number().map(str::to_owned),
            interface_number: info.interface_number(),
            usage_page: info.usage_page(),
            usage: info.usage(),
            is_vendor_interface: true,
        });
    }

    out
}

/// Resolve the vendor Feature interface for `kind`, falling back to the first
/// matching interface (mirrors `scripts/mouse_proto.py`).
fn find_vendor_path(api: &HidApi, kind: DeviceKind) -> Result<CString> {
    let pid = kind.pid();
    let mut interface_fallback: Option<CString> = None;
    let mut fallback: Option<CString> = None;

    for info in api.device_list() {
        if info.vendor_id() != protocol::VID || info.product_id() != pid {
            continue;
        }
        // The vendor Feature collection is the one the protocol uses; the
        // interface number alone is ambiguous because every collection of the
        // same interface shares it.
        if info.usage_page() == protocol::USAGE_PAGE {
            return Ok(info.path().to_owned());
        }
        if interface_fallback.is_none() && info.interface_number() == protocol::HID_INTERFACE {
            interface_fallback = Some(info.path().to_owned());
        }
        if fallback.is_none() {
            fallback = Some(info.path().to_owned());
        }
    }

    interface_fallback
        .or(fallback)
        .ok_or(Error::DeviceNotFound)
}

fn need_byte(data: &[u8], off: usize) -> Result<u8> {
    data.get(off)
        .copied()
        .ok_or(Error::Protocol(ProtocolError::BadReply))
}

fn need_u16(data: &[u8], off: usize) -> Result<u16> {
    protocol::u16_le(data, off).ok_or(Error::Protocol(ProtocolError::BadReply))
}

/// Firmware/protocol version (`PROTO_CMD_GET_VERSION`).
#[derive(Debug, Clone, Serialize)]
pub struct Version {
    pub protocol: u8,
    pub fw_major: u8,
    pub fw_minor: u8,
    pub fw_patch: u8,
}

/// Capability bitmap (`PROTO_CMD_GET_INFO`).
#[derive(Debug, Clone, Serialize)]
pub struct Capabilities {
    pub raw: u16,
    pub sensor: bool,
    pub radio: bool,
    pub battery: bool,
    pub bio: bool,
}

/// Battery state (`PROTO_CMD_GET_BATTERY`).
#[derive(Debug, Clone, Serialize)]
pub struct Battery {
    pub voltage_mv: u16,
    pub percent: u8,
    pub charging: bool,
    pub power_good: bool,
    pub fault: bool,
}

/// Underglow state (`PROTO_CMD_GET/SET_RGB`).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Rgb {
    pub enable: u8,
    pub effect: u8,
    pub brightness: u8,
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

/// The dashboard snapshot: version, capabilities, radio and battery.
#[derive(Debug, Clone, Serialize)]
pub struct Status {
    pub version: Version,
    pub capabilities: Capabilities,
    pub radio: String,
    pub battery: Battery,
}

/// Report rates for all three links.
#[derive(Debug, Clone, Serialize)]
pub struct Rates {
    pub usb: u16,
    pub rf: u16,
    pub ble: u16,
}

/// Every get-command aggregated, for the settings pages.
#[derive(Debug, Clone, Serialize)]
pub struct Settings {
    pub version: Version,
    pub capabilities: Capabilities,
    pub radio: String,
    pub battery: Battery,
    pub dpi: u16,
    pub sensor: u8,
    pub sensor_label: String,
    pub lift: u8,
    pub lift_label: String,
    pub air_sens: u8,
    pub air_sens_pair: [u16; 2],
    pub air_odr: u8,
    pub air_odr_hz: u16,
    pub rgb: Rgb,
    pub oled: bool,
    pub periph_batt: bool,
    pub motor_enable: bool,
    pub motor_running: bool,
    pub rates: Rates,
}

/// Result of the raw-console escape hatch; unlike the typed commands a non-OK
/// status is data, not an error.
#[derive(Debug, Clone, Serialize)]
pub struct RawReply {
    pub status: u8,
    pub status_label: String,
    pub data: Vec<u8>,
}

/// An open connection to the mouse or dongle.
pub struct Device {
    transport: Transport,
    kind: DeviceKind,
    seq: u8,
    link: Link,
}

impl Device {
    /// Open the vendor Feature interface for `kind`, or an explicit `path`.
    pub fn open(api: &HidApi, kind: DeviceKind, path: Option<&str>) -> Result<Self> {
        let transport = match path {
            Some(p) if !p.is_empty() => {
                let cpath = CString::new(p)?;
                Transport::open_path(api, &cpath)?
            }
            _ => {
                let cpath = find_vendor_path(api, kind)?;
                Transport::open_path(api, &cpath)?
            }
        };

        Ok(Device {
            transport,
            kind,
            seq: 0,
            link: kind.link(),
        })
    }

    pub fn kind(&self) -> DeviceKind {
        self.kind
    }

    pub fn link(&self) -> Link {
        self.link
    }

    fn next_seq(&mut self) -> u8 {
        self.seq = self.seq.wrapping_add(1);
        self.seq
    }

    /// One typed transaction: build, send, poll, and reject a non-OK status.
    ///
    /// The RF link drops a reply now and then; a timed-out request is retried a
    /// few times. The device re-runs the command, so this is safe because the
    /// getters and the setters are idempotent - the one-shot buzz is excluded.
    fn request(&mut self, cmd: Command, payload: &[u8]) -> Result<Vec<u8>> {
        const ATTEMPTS: u8 = 3;
        let attempts = if matches!(cmd, Command::SetMotor) {
            1
        } else {
            ATTEMPTS
        };

        let mut attempt = 0u8;
        loop {
            attempt += 1;
            let seq = self.next_seq();
            match self
                .transport
                .transact(cmd.code(), seq, payload, self.kind.timeout())
            {
                Ok(reply) if !reply.status.is_ok() => {
                    return Err(Error::Status(reply.status));
                }
                Ok(reply) => return Ok(reply.data),
                Err(Error::Timeout { .. }) if attempt < attempts => continue,
                Err(e) => return Err(e),
            }
        }
    }

    // ---- status -------------------------------------------------------

    pub fn version(&mut self) -> Result<Version> {
        let d = self.request(Command::GetVersion, &[])?;
        Ok(Version {
            protocol: need_byte(&d, 0)?,
            fw_major: need_byte(&d, 1)?,
            fw_minor: need_byte(&d, 2)?,
            fw_patch: need_byte(&d, 3)?,
        })
    }

    pub fn capabilities(&mut self) -> Result<Capabilities> {
        let d = self.request(Command::GetInfo, &[])?;
        let raw = need_u16(&d, 0)?;
        Ok(Capabilities {
            raw,
            sensor: raw & 0x01 != 0,
            radio: raw & 0x02 != 0,
            battery: raw & 0x04 != 0,
            bio: raw & 0x08 != 0,
        })
    }

    pub fn radio(&mut self) -> Result<String> {
        let d = self.request(Command::GetRadio, &[])?;
        Ok(if need_byte(&d, 0)? == 0 { "rf" } else { "ble" }.to_string())
    }

    pub fn battery(&mut self) -> Result<Battery> {
        let d = self.request(Command::GetBattery, &[])?;
        let flags = need_byte(&d, 3)?;
        Ok(Battery {
            voltage_mv: need_u16(&d, 0)?,
            percent: need_byte(&d, 2)?,
            charging: flags & 0x01 != 0,
            power_good: flags & 0x02 != 0,
            fault: flags & 0x04 != 0,
        })
    }

    pub fn status(&mut self) -> Result<Status> {
        Ok(Status {
            version: self.version()?,
            capabilities: self.capabilities()?,
            radio: self.radio()?,
            battery: self.battery()?,
        })
    }

    // ---- sensor -------------------------------------------------------

    pub fn dpi(&mut self) -> Result<u16> {
        let d = self.request(Command::GetDpi, &[])?;
        need_u16(&d, 0)
    }

    pub fn set_dpi(&mut self, cpi: u16) -> Result<u16> {
        let d = self.request(Command::SetDpi, &cpi.to_le_bytes())?;
        need_u16(&d, 0)
    }

    pub fn sensor(&mut self) -> Result<u8> {
        let d = self.request(Command::GetSensor, &[])?;
        need_byte(&d, 0)
    }

    pub fn set_sensor(&mut self, mode: u8) -> Result<u8> {
        let d = self.request(Command::SetSensor, &[mode])?;
        need_byte(&d, 0)
    }

    pub fn lift(&mut self) -> Result<u8> {
        let d = self.request(Command::GetLift, &[])?;
        need_byte(&d, 0)
    }

    pub fn set_lift(&mut self, cut: u8) -> Result<u8> {
        let d = self.request(Command::SetLift, &[cut])?;
        need_byte(&d, 0)
    }

    // ---- air mouse ----------------------------------------------------

    pub fn air_sens(&mut self) -> Result<u8> {
        let d = self.request(Command::GetAirSens, &[])?;
        need_byte(&d, 0)
    }

    pub fn set_air_sens(&mut self, idx: u8) -> Result<u8> {
        let d = self.request(Command::SetAirSens, &[idx])?;
        need_byte(&d, 0)
    }

    pub fn air_odr(&mut self) -> Result<u8> {
        let d = self.request(Command::GetAirOdr, &[])?;
        need_byte(&d, 0)
    }

    pub fn set_air_odr(&mut self, idx: u8) -> Result<u8> {
        let d = self.request(Command::SetAirOdr, &[idx])?;
        need_byte(&d, 0)
    }

    // ---- lighting -----------------------------------------------------

    pub fn rgb(&mut self) -> Result<Rgb> {
        let d = self.request(Command::GetRgb, &[])?;
        Ok(Rgb {
            enable: need_byte(&d, 0)?,
            effect: need_byte(&d, 1)?,
            brightness: need_byte(&d, 2)?,
            r: need_byte(&d, 3)?,
            g: need_byte(&d, 4)?,
            b: need_byte(&d, 5)?,
        })
    }

    pub fn set_rgb(&mut self, rgb: &Rgb) -> Result<Rgb> {
        let payload = [rgb.enable, rgb.effect, rgb.brightness, rgb.r, rgb.g, rgb.b];
        let d = self.request(Command::SetRgb, &payload)?;
        Ok(Rgb {
            enable: need_byte(&d, 0)?,
            effect: need_byte(&d, 1)?,
            brightness: need_byte(&d, 2)?,
            r: need_byte(&d, 3)?,
            g: need_byte(&d, 4)?,
            b: need_byte(&d, 5)?,
        })
    }

    // ---- display / peripherals ---------------------------------------

    pub fn oled(&mut self) -> Result<bool> {
        let d = self.request(Command::GetOled, &[])?;
        Ok(need_byte(&d, 0)? != 0)
    }

    pub fn set_oled(&mut self, on: bool) -> Result<bool> {
        let d = self.request(Command::SetOled, &[on as u8])?;
        Ok(need_byte(&d, 0)? != 0)
    }

    pub fn periph_batt(&mut self) -> Result<bool> {
        let d = self.request(Command::GetPeriph, &[])?;
        Ok(need_byte(&d, 0)? != 0)
    }

    pub fn set_periph_batt(&mut self, on: bool) -> Result<bool> {
        let d = self.request(Command::SetPeriph, &[on as u8])?;
        Ok(need_byte(&d, 0)? != 0)
    }

    // ---- motor --------------------------------------------------------

    pub fn motor_running(&mut self) -> Result<bool> {
        let d = self.request(Command::GetMotor, &[])?;
        Ok(need_byte(&d, 0)? != 0)
    }

    pub fn buzz(&mut self, ms: u16) -> Result<bool> {
        let d = self.request(Command::SetMotor, &ms.to_le_bytes())?;
        Ok(need_byte(&d, 0)? != 0)
    }

    pub fn motor_enable(&mut self) -> Result<bool> {
        let d = self.request(Command::GetMotorEn, &[])?;
        Ok(need_byte(&d, 0)? != 0)
    }

    pub fn set_motor_enable(&mut self, on: bool) -> Result<bool> {
        let d = self.request(Command::SetMotorEn, &[on as u8])?;
        Ok(need_byte(&d, 0)? != 0)
    }

    // ---- report rate --------------------------------------------------

    pub fn rate(&mut self, link: Link) -> Result<u16> {
        let d = self.request(Command::GetRate, &[link.code()])?;
        need_u16(&d, 0)
    }

    pub fn set_rate(&mut self, link: Link, hz: u16) -> Result<u16> {
        let mut payload = [0u8; 3];
        payload[0] = link.code();
        payload[1..3].copy_from_slice(&hz.to_le_bytes());
        let d = self.request(Command::SetRate, &payload)?;
        need_u16(&d, 0)
    }

    // ---- BIO ----------------------------------------------------------

    pub fn bio_acq(&mut self, on: bool) -> Result<()> {
        self.request(Command::BioAcq, &[on as u8])?;
        Ok(())
    }

    pub fn bio_sleep(&mut self, on: bool) -> Result<()> {
        self.request(Command::BioSleep, &[on as u8])?;
        Ok(())
    }

    // ---- aggregate ----------------------------------------------------

    pub fn settings(&mut self) -> Result<Settings> {
        let version = self.version()?;
        let capabilities = self.capabilities()?;
        let radio = self.radio()?;
        let battery = self.battery()?;
        let dpi = self.dpi()?;
        let sensor = self.sensor()?;
        let lift = self.lift()?;
        let air_sens = self.air_sens()?;
        let air_odr = self.air_odr()?;
        let rgb = self.rgb()?;
        let oled = self.oled()?;
        let periph_batt = self.periph_batt()?;
        let motor_enable = self.motor_enable()?;
        let motor_running = self.motor_running()?;
        let rates = Rates {
            usb: self.rate(Link::Usb)?,
            rf: self.rate(Link::Rf)?,
            ble: self.rate(Link::Ble)?,
        };

        let sensor_label = SENSOR_MODES
            .get(sensor as usize)
            .copied()
            .unwrap_or("unknown")
            .to_string();
        let lift_label = LIFT_CUTS
            .get(lift as usize)
            .copied()
            .unwrap_or("unknown")
            .to_string();
        let air_sens_pair = AIR_SENS
            .get(air_sens as usize)
            .copied()
            .map(|(x, y)| [x, y])
            .unwrap_or([0, 0]);
        let air_odr_hz = AIR_ODR_HZ.get(air_odr as usize).copied().unwrap_or(0);

        Ok(Settings {
            version,
            capabilities,
            radio,
            battery,
            dpi,
            sensor,
            sensor_label,
            lift,
            lift_label,
            air_sens,
            air_sens_pair,
            air_odr,
            air_odr_hz,
            rgb,
            oled,
            periph_batt,
            motor_enable,
            motor_running,
            rates,
        })
    }

    // ---- raw ----------------------------------------------------------

    /// Send an arbitrary command with an arbitrary payload. Returns the status
    /// verbatim, so the raw console can see NACKs instead of an error.
    pub fn raw(&mut self, cmd: u8, payload: &[u8]) -> Result<RawReply> {
        let seq = self.next_seq();
        let reply = self
            .transport
            .transact(cmd, seq, payload, self.kind.timeout())?;
        Ok(RawReply {
            status: reply.status as u8,
            status_label: reply.status.label().to_string(),
            data: reply.data,
        })
    }
}
