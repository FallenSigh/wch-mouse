//! `#[tauri::command]` handlers exposed to the frontend.
//!
//! Every command that touches the device is `async` and hops to a blocking
//! thread, because a transact can stall for up to 400 ms over the dongle and
//! must not block the UI (or the async runtime worker it lands on).

use std::sync::{Arc, Mutex};

use hidapi::HidApi;
use tauri::{AppHandle, State};

use crate::bio::{self, BioState, SerialPortInfo};
use crate::device::{self, Device, DeviceInfo, DeviceKind, RawReply, Rgb, Settings, Status};
use crate::error::{Error, Result};

type CmdResult<T> = std::result::Result<T, String>;

/// Shared app state: at most one open device at a time.
#[derive(Default)]
pub struct AppState {
    pub device: Arc<Mutex<Option<Device>>>,
}

fn lock_error() -> Error {
    Error::InvalidArgument("device state lock is poisoned".to_string())
}

/// Run `f` against the connected device on a blocking thread.
async fn with_device<T, F>(state: &AppState, f: F) -> CmdResult<T>
where
    T: Send + 'static,
    F: FnOnce(&mut Device) -> Result<T> + Send + 'static,
{
    let slot = state.device.clone();
    tauri::async_runtime::spawn_blocking(move || {
        let mut guard = slot.lock().map_err(|_| lock_error())?;
        let dev = guard.as_mut().ok_or(Error::NotConnected)?;
        f(dev)
    })
    .await
    .map_err(|e| e.to_string())?
    .map_err(|e| e.to_string())
}

// ---- connection -------------------------------------------------------

/// Enumerate every mouse/dongle HID interface.
#[tauri::command]
pub async fn list_devices() -> CmdResult<Vec<DeviceInfo>> {
    tauri::async_runtime::spawn_blocking(|| {
        let api = HidApi::new()?;
        Ok::<_, Error>(device::list_devices(&api))
    })
    .await
    .map_err(|e| e.to_string())?
    .map_err(|e| e.to_string())
}

/// Open the device. `pid_kind` is `"mouse"` or `"dongle"`; `path` optionally
/// forces a specific HID path from [`list_devices`].
#[tauri::command]
pub async fn connect(
    state: State<'_, AppState>,
    pid_kind: String,
    path: Option<String>,
) -> CmdResult<()> {
    let slot = state.device.clone();
    tauri::async_runtime::spawn_blocking(move || {
        let kind = DeviceKind::parse(&pid_kind)?;
        let api = HidApi::new()?;
        let dev = Device::open(&api, kind, path.as_deref())?;
        let mut guard = slot.lock().map_err(|_| lock_error())?;
        *guard = Some(dev);
        Ok::<_, Error>(())
    })
    .await
    .map_err(|e| e.to_string())?
    .map_err(|e| e.to_string())
}

/// Drop the current connection.
#[tauri::command]
pub async fn disconnect(state: State<'_, AppState>) -> CmdResult<()> {
    let slot = state.device.clone();
    tauri::async_runtime::spawn_blocking(move || {
        let mut guard = slot.lock().map_err(|_| lock_error())?;
        *guard = None;
        Ok::<_, Error>(())
    })
    .await
    .map_err(|e| e.to_string())?
    .map_err(|e| e.to_string())
}

// ---- status / aggregate ----------------------------------------------

#[tauri::command]
pub async fn status(state: State<'_, AppState>) -> CmdResult<Status> {
    with_device(state.inner(), |d| d.status()).await
}

#[tauri::command]
pub async fn get_settings(state: State<'_, AppState>) -> CmdResult<Settings> {
    with_device(state.inner(), |d| d.settings()).await
}

// ---- setters ----------------------------------------------------------

#[tauri::command]
pub async fn set_dpi(state: State<'_, AppState>, cpi: u16) -> CmdResult<u16> {
    with_device(state.inner(), move |d| d.set_dpi(cpi)).await
}

#[tauri::command]
pub async fn set_sensor(state: State<'_, AppState>, mode: u8) -> CmdResult<u8> {
    with_device(state.inner(), move |d| d.set_sensor(mode)).await
}

#[tauri::command]
pub async fn set_lift(state: State<'_, AppState>, cut: u8) -> CmdResult<u8> {
    with_device(state.inner(), move |d| d.set_lift(cut)).await
}

#[tauri::command]
pub async fn set_air_sens(state: State<'_, AppState>, idx: u8) -> CmdResult<u8> {
    with_device(state.inner(), move |d| d.set_air_sens(idx)).await
}

#[tauri::command]
pub async fn set_air_odr(state: State<'_, AppState>, idx: u8) -> CmdResult<u8> {
    with_device(state.inner(), move |d| d.set_air_odr(idx)).await
}

#[tauri::command]
pub async fn set_rgb(state: State<'_, AppState>, rgb: Rgb) -> CmdResult<Rgb> {
    with_device(state.inner(), move |d| d.set_rgb(&rgb)).await
}

#[tauri::command]
pub async fn set_oled(state: State<'_, AppState>, on: bool) -> CmdResult<bool> {
    with_device(state.inner(), move |d| d.set_oled(on)).await
}

/// `link` is the protocol link selector: 0 usb, 1 rf, 2 ble.
#[tauri::command]
pub async fn set_rate(state: State<'_, AppState>, link: u8, hz: u16) -> CmdResult<u16> {
    let link = crate::protocol::Link::from_code(link)
        .ok_or_else(|| Error::InvalidArgument(format!("unknown link selector {link}")))
        .map_err(|e| e.to_string())?;
    with_device(state.inner(), move |d| d.set_rate(link, hz)).await
}

#[tauri::command]
pub async fn set_periph(state: State<'_, AppState>, on: bool) -> CmdResult<bool> {
    with_device(state.inner(), move |d| d.set_periph_batt(on)).await
}

#[tauri::command]
pub async fn set_motor_en(state: State<'_, AppState>, on: bool) -> CmdResult<bool> {
    with_device(state.inner(), move |d| d.set_motor_enable(on)).await
}

#[tauri::command]
pub async fn buzz(state: State<'_, AppState>, ms: u16) -> CmdResult<bool> {
    with_device(state.inner(), move |d| d.buzz(ms)).await
}

#[tauri::command]
pub async fn bio_acq(state: State<'_, AppState>, on: bool) -> CmdResult<()> {
    with_device(state.inner(), move |d| d.bio_acq(on)).await
}

#[tauri::command]
pub async fn bio_sleep(state: State<'_, AppState>, on: bool) -> CmdResult<()> {
    with_device(state.inner(), move |d| d.bio_sleep(on)).await
}

// ---- BIO live monitor -------------------------------------------------

/// Enumerate the wired mouse's CDC serial ports (the dongle exposes none).
#[tauri::command]
pub async fn list_serial_ports() -> CmdResult<Vec<SerialPortInfo>> {
    tauri::async_runtime::spawn_blocking(bio::list_serial_ports)
        .await
        .map_err(|e| e.to_string())?
        .map_err(|e| e.to_string())
}

/// Enable BIO acquisition over HID and stream the selected serial port.
#[tauri::command]
pub async fn bio_start(
    app: AppHandle,
    hid: State<'_, AppState>,
    bio: State<'_, BioState>,
    port: String,
) -> CmdResult<()> {
    let slot = hid.device.clone();
    let state = bio.inner().clone();
    tauri::async_runtime::spawn_blocking(move || {
        state.stop();
        {
            let mut guard = slot.lock().map_err(|_| lock_error())?;
            let dev = guard.as_mut().ok_or(Error::NotConnected)?;
            dev.bio_sleep(false)?;
            dev.bio_acq(true)?;
        }
        state.start(app, &port)
    })
    .await
    .map_err(|e| e.to_string())?
    .map_err(|e| e.to_string())
}

/// Stop the serial reader, then disable BIO acquisition over HID.
#[tauri::command]
pub async fn bio_stop(hid: State<'_, AppState>, bio: State<'_, BioState>) -> CmdResult<()> {
    let slot = hid.device.clone();
    let state = bio.inner().clone();
    tauri::async_runtime::spawn_blocking(move || {
        state.stop();
        let mut guard = slot.lock().map_err(|_| lock_error())?;
        if let Some(dev) = guard.as_mut() {
            dev.bio_acq(false)?;
        }
        Ok::<_, Error>(())
    })
    .await
    .map_err(|e| e.to_string())?
    .map_err(|e| e.to_string())
}

// ---- raw --------------------------------------------------------------

/// Send an arbitrary command with a byte payload; returns the raw status.
#[tauri::command]
pub async fn raw(state: State<'_, AppState>, cmd: u8, payload: Vec<u8>) -> CmdResult<RawReply> {
    with_device(state.inner(), move |d| d.raw(cmd, &payload)).await
}
