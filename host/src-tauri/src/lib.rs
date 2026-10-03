//! Tauri entry point: registers the app state and every command.

mod bio;
mod bio_frame;
mod commands;
mod device;
mod error;
mod protocol;
mod transport;

use commands::AppState;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(AppState::default())
        .manage(bio::BioState::default())
        .invoke_handler(tauri::generate_handler![
            commands::list_devices,
            commands::connect,
            commands::disconnect,
            commands::status,
            commands::get_settings,
            commands::set_dpi,
            commands::set_sensor,
            commands::set_lift,
            commands::set_air_sens,
            commands::set_air_odr,
            commands::set_rgb,
            commands::set_oled,
            commands::set_rate,
            commands::set_periph,
            commands::set_motor_en,
            commands::buzz,
            commands::bio_acq,
            commands::bio_sleep,
            commands::list_serial_ports,
            commands::bio_start,
            commands::bio_stop,
            commands::raw,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
