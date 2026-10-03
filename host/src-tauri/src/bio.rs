//! BIO live monitor: enumerate the wired mouse's CDC serial port, stream JFC103
//! real-time packets off it, and emit them to the frontend.
//!
//! The pure 88-byte parser/framer lives in [`crate::bio_frame`] so it can be
//! tested without Tauri or serialport; this module adds the serde facade, the
//! port enumeration, and the background reader thread.

use std::io::{self, Read};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

use serde::ser::{SerializeStruct, Serializer};
use serde::Serialize;
use tauri::{AppHandle, Emitter};

use crate::error::Result;
use crate::protocol::{PID_MOUSE, VID};

pub use crate::bio_frame::{BioPacket, Framer};

/// JFC103 link speed (matches the module's UART framing).
const BAUD_RATE: u32 = 38400;
/// Read timeout so the loop can observe the stop flag promptly.
const READ_TIMEOUT_MS: u64 = 100;
/// Read buffer, comfortably larger than one packet.
const READ_BUF: usize = 256;

/// Event carrying one decoded packet.
pub const EVENT_PACKET: &str = "bio:packet";
/// Event carrying a fatal reader error.
pub const EVENT_ERROR: &str = "bio:error";

/// One serial port exposed by the wired mouse's USB CDC interface.
#[derive(Debug, Clone, Serialize)]
pub struct SerialPortInfo {
    pub port_name: String,
    pub product: Option<String>,
    pub serial_number: Option<String>,
}

impl Serialize for BioPacket {
    fn serialize<S>(&self, serializer: S) -> std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let mut s = serializer.serialize_struct("BioPacket", 16)?;
        s.serialize_field("acdata", &self.acdata)?;
        s.serialize_field("heartrate", &self.heartrate)?;
        s.serialize_field("spo2", &self.spo2)?;
        s.serialize_field("bk", &self.bk)?;
        s.serialize_field("fatigue", &self.fatigue)?;
        s.serialize_field("systolic", &self.systolic)?;
        s.serialize_field("diastolic", &self.diastolic)?;
        s.serialize_field("cardiac_output", &self.cardiac_output)?;
        s.serialize_field("peripheral_resistance", &self.peripheral_resistance)?;
        s.serialize_field("rr", &self.rr)?;
        s.serialize_field("sdnn", &self.sdnn)?;
        s.serialize_field("rmssd", &self.rmssd)?;
        s.serialize_field("nn50", &self.nn50)?;
        s.serialize_field("pnn50", &self.pnn50)?;
        s.serialize_field("rra", &self.rra)?;
        s.serialize_field("valid", &self.valid)?;
        s.end()
    }
}

/// Enumerate serial ports, keeping only the wired mouse's USB CDC interface.
///
/// The dongle has no CDC interface, so this returns an empty list when only the
/// dongle is attached.
pub fn list_serial_ports() -> Result<Vec<SerialPortInfo>> {
    let mut out = Vec::new();
    for port in serialport::available_ports()? {
        if let serialport::SerialPortType::UsbPort(usb) = port.port_type {
            if usb.vid == VID && usb.pid == PID_MOUSE {
                out.push(SerialPortInfo {
                    port_name: port.port_name,
                    product: usb.product,
                    serial_number: usb.serial_number,
                });
            }
        }
    }
    Ok(out)
}

/// The running reader: its stop flag and the thread that owns the port.
#[derive(Default)]
struct Reader {
    stop: Option<Arc<AtomicBool>>,
    handle: Option<JoinHandle<()>>,
}

/// Managed state owning the running reader so `bio_stop` can end it.
#[derive(Default, Clone)]
pub struct BioState {
    inner: Arc<Mutex<Reader>>,
}

impl BioState {
    /// Stop the reader thread if one is running, and wait for it to exit.
    pub fn stop(&self) {
        let (stop, handle) = {
            let mut guard = self.inner.lock().unwrap_or_else(|e| e.into_inner());
            (guard.stop.take(), guard.handle.take())
        };
        if let Some(stop) = stop {
            stop.store(true, Ordering::Relaxed);
        }
        if let Some(handle) = handle {
            let _ = handle.join();
        }
    }

    /// Open `port_name` and spawn the reader. Caller must [`BioState::stop`] any
    /// previous reader first.
    pub fn start(&self, app: AppHandle, port_name: &str) -> Result<()> {
        let port = serialport::new(port_name, BAUD_RATE)
            .timeout(Duration::from_millis(READ_TIMEOUT_MS))
            .open()?;
        let stop = Arc::new(AtomicBool::new(false));
        let handle = spawn_reader(app, port, stop.clone());

        let mut guard = self.inner.lock().unwrap_or_else(|e| e.into_inner());
        guard.stop = Some(stop);
        guard.handle = Some(handle);
        Ok(())
    }
}

/// Read the port until `stop` is set, emitting every decoded packet.
fn spawn_reader(
    app: AppHandle,
    mut port: Box<dyn serialport::SerialPort>,
    stop: Arc<AtomicBool>,
) -> JoinHandle<()> {
    thread::spawn(move || {
        let mut framer = Framer::new();
        let mut buf = [0u8; READ_BUF];

        while !stop.load(Ordering::Relaxed) {
            match port.read(&mut buf) {
                Ok(0) => {}
                Ok(n) => {
                    for &byte in &buf[..n] {
                        if let Some(packet) = framer.push(byte) {
                            let _ = app.emit(EVENT_PACKET, &packet);
                        }
                    }
                }
                Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {}
                Err(e) => {
                    let _ = app.emit(EVENT_ERROR, format!("serial read failed: {e}"));
                    break;
                }
            }
        }
        // Dropping `port` here closes the handle.
    })
}
