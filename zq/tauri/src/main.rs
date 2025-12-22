// TODO
//  - Setup tray icon image
//  - Clean up log output
//  - Add GUI to monitor hid messages.
use hidapi::{DeviceInfo, HidApi};
use serde::Serialize;
use std::{
    sync::Mutex,
    thread,
    time::Duration,
};
use tauri::{
    menu::MenuBuilder,
    tray::{TrayIconBuilder, TrayIcon},
    Emitter, Manager, WindowEvent,
    WebviewWindowBuilder, WebviewUrl,
};

const RAW_APP_ID_ZQ: u8 = 0xFF;
const RAW_REPORT_TYPE_LAYER: u8 = 0x00;
const RAW_REPORT_VERSION: u8 = 0x01;

const DEFAULT_VENDOR_ID: u16 = 0x5957;
const DEFAULT_PRODUCT_ID: u16 = 0x0200;
const DEFAULT_USAGE_PAGE: u16 = 0xFF60;
const DEFAULT_USAGE: u16 = 0x61;
const ENUM_POLL_INTERVAL_MS: u64 = 500;

const TRAY_MENU_SETTINGS_ID: &str = "settings";
const TRAY_MENU_QUIT_ID: &str = "quit";
const SETTINGS_WINDOW_LABEL: &str = "settings";

struct AppSettings {
    hid: Mutex<HidSettings>,
}

#[derive(Debug, Clone, Copy, Serialize, serde::Deserialize, PartialEq, Eq)]
struct HidSettings {
    vendor_id: u16,
    product_id: u16,
    usage_page: u16,
    usage: u16,
}

impl Default for HidSettings {
    fn default() -> Self {
        Self {
            vendor_id: DEFAULT_VENDOR_ID,
            product_id: DEFAULT_PRODUCT_ID,
            usage_page: DEFAULT_USAGE_PAGE,
            usage: DEFAULT_USAGE,
        }
    }
}

#[derive(Debug, Serialize)]
struct LayerReport {
    version: u8,
    highest_layer: u8,
    mask: u8, // bitmask for layers 0-7 (intentionally limited)
    raw: Vec<u8>,
}

fn parse_layer_report(data: &[u8]) -> Option<LayerReport> {
    if data.len() < 5 {
        return None;
    }
    if data[0] != RAW_APP_ID_ZQ || data[1] != RAW_REPORT_VERSION || data[2] != RAW_REPORT_TYPE_LAYER {
        return None;
    }
    Some(LayerReport {
        version: data[1],
        highest_layer: data[3],
        mask: data[4],
        raw: data.to_vec(),
    })
}

fn format_wchar_raw<T: std::fmt::LowerHex + Copy>(raw: Option<&[T]>) -> String {
    match raw {
        Some(slice) if !slice.is_empty() => slice
            .iter()
            .map(|c| format!("{:04x}", c))
            .collect::<Vec<_>>()
            .join(" "),
        Some(_) => "empty".to_string(),
        None => "-".to_string(),
    }
}

fn log_device_list(devices: &[DeviceInfo]) {
    println!("[hid] device list ({} entries):", devices.len());

    for (idx, dev) in devices.iter().enumerate() {
        #[cfg(not(all(libusb, target_os = "linux")))]
        let usage_info = format!(
            "usage_page=0x{:04x} usage=0x{:04x}",
            dev.usage_page(),
            dev.usage()
        );
        #[cfg(all(libusb, target_os = "linux"))]
        let usage_info = "usage_page/usage=N/A (libusb linux)".to_string();

        println!(
            "[hid] #{idx}: {:04x}:{:04x} path:{} iface:{} rel:0x{:04x} bus:{:?} {usage_info}",
            dev.vendor_id(),
            dev.product_id(),
            dev.path().to_string_lossy(),
            dev.interface_number(),
            dev.release_number(),
            dev.bus_type(),
        );
        println!(
            "       mfr:{:?} (raw:{}) prod:{:?} (raw:{}) serial:{:?} (raw:{})",
            dev.manufacturer_string(),
            format_wchar_raw(dev.manufacturer_string_raw()),
            dev.product_string(),
            format_wchar_raw(dev.product_string_raw()),
            dev.serial_number(),
            format_wchar_raw(dev.serial_number_raw()),
        );
    }
}

fn update_tray_status(app: &tauri::AppHandle, report: &LayerReport) {
    let active_layers = mask_to_layer_list(report.mask);
    let text = format!(
        "layer={} active=[{}]",
        report.highest_layer, active_layers
    );
    if let Some(tray) = app.try_state::<TrayIcon>() {
        let _ = tray.set_tooltip(Some(text.as_str()));
        let _ = tray.set_title(Some(text.as_str()));
    }
}

fn mask_to_layer_list(mask: u8) -> String {
    let mut layers = Vec::new();
    for bit in 0..8 {
        if (mask & (1 << bit)) != 0 {
            layers.push(bit.to_string());
        }
    }
    layers.join(",")
}

fn open_settings_window(app: &tauri::AppHandle) {
    if let Some(window) = app.get_webview_window(SETTINGS_WINDOW_LABEL) {
        let _ = window.show();
        let _ = window.set_focus();
        return;
    }

    let _ = WebviewWindowBuilder::new(
        app,
        SETTINGS_WINDOW_LABEL,
        WebviewUrl::App("settings.html".into()),
    )
    .title("ZQ - Settings")
    .inner_size(320.0, 500.0)
    .resizable(false)
    .decorations(false)
    .build();
}

#[tauri::command]
fn get_hid_settings(app: tauri::AppHandle) -> HidSettings {
    app.state::<AppSettings>()
        .hid
        .lock()
        .map(|guard| *guard)
        .unwrap_or_default()
}

#[tauri::command]
fn set_hid_settings(app: tauri::AppHandle, settings: HidSettings) -> Result<(), String> {
    let state = app.state::<AppSettings>();
    let mut guard = state
        .hid
        .lock()
        .map_err(|_| "settings lock poisoned".to_string())?;
    *guard = settings;
    Ok(())
}

#[tauri::command]
fn hide_settings_window(app: tauri::AppHandle) {
    if let Some(window) = app.get_webview_window(SETTINGS_WINDOW_LABEL) {
        let _ = window.hide();
    }
}

fn read_hid_settings(app: &tauri::AppHandle) -> HidSettings {
    app.state::<AppSettings>()
        .hid
        .lock()
        .map(|guard| *guard)
        .unwrap_or_default()
}

fn spawn_hid_listener(app: tauri::AppHandle) {
    thread::spawn(move || loop {
        let mut api = match HidApi::new() {
            Ok(api) => api,
            Err(e) => {
                eprintln!("[hid] init error: {e}");
                thread::sleep(Duration::from_millis(ENUM_POLL_INTERVAL_MS));
                continue;
            }
        };

        let mut device = None;
        let mut buf    = [0u8; 64];
        let mut active_settings = read_hid_settings(&app);

        loop {
            let current_settings = read_hid_settings(&app);
            if current_settings != active_settings {
                active_settings = current_settings;
                device = None;
            }
            if device.is_none() {
                let _ = api.refresh_devices();
                let devices: Vec<DeviceInfo> = api.device_list().cloned().collect();
                log_device_list(&devices);

                if let Some(info) = devices.iter().find(|d| {
                    let usage      = d.usage();
                    let usage_page = d.usage_page();
                    let usage_ok = if usage == 0 && usage_page == 0 {
                        // Some backends (e.g., Linux hidraw) may not expose usage; fall back to VID/PID match.
                        true
                    } else {
                        usage == active_settings.usage && usage_page == active_settings.usage_page
                    };
                    d.vendor_id() == active_settings.vendor_id
                        && d.product_id() == active_settings.product_id
                        && usage_ok
                }) {
                    match info.open_device(&api) {
                        Ok(dev) => {
                            let _ = dev.set_blocking_mode(false);
                            println!(
                                "[hid] connected to {:04x}:{:04x}",
                                active_settings.vendor_id,
                                active_settings.product_id
                            );
                            device = Some(dev);
                        }
                        Err(e) => eprintln!(
                            "[hid] open error {:04x}:{:04x}: {e}",
                            active_settings.vendor_id,
                            active_settings.product_id
                        ),
                    }
                }
                if device.is_none() {
                    thread::sleep(Duration::from_millis(ENUM_POLL_INTERVAL_MS));
                    continue;
                }
            }

            let dev = device.as_ref().expect("device just checked");
            match dev.read_timeout(&mut buf, 300) {
                Ok(len) if len > 0 => {
                    let data = &buf[..len];
                    println!("[hid] rx {len} bytes: {:02x?}", data);
                    if let Some(report) = parse_layer_report(data) {
                        println!(
                            "[hid] layer -> highest:{} mask:0b{:08b} version:{}",
                            report.highest_layer, report.mask, report.version
                        );
                        let _ = app.emit("zq://hid/layer_state", &report);
                        update_tray_status(&app, &report);
                    }
                }
                Ok(_) => {}
                Err(e) => {
                    eprintln!("[hid] read error (will re-enumerate): {e}");
                    device = None;
                    thread::sleep(Duration::from_millis(ENUM_POLL_INTERVAL_MS));
                }
            }
        }
    });
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            get_hid_settings,
            set_hid_settings,
            hide_settings_window
        ])
        .setup(|app| {
            app.manage(AppSettings {
                hid: Mutex::new(HidSettings::default()),
            });
            let tray_menu = MenuBuilder::new(app)
                .text(TRAY_MENU_SETTINGS_ID, "Settings")
                .separator()
                .text(TRAY_MENU_QUIT_ID, "Quit")
                .build()?;
            let tray = TrayIconBuilder::new()
                .menu(&tray_menu)
                .tooltip("ZQ HID Logger")
                .show_menu_on_left_click(false)
                .on_menu_event(|app, event| match event.id().as_ref() {
                    TRAY_MENU_SETTINGS_ID => open_settings_window(app),
                    TRAY_MENU_QUIT_ID => app.exit(0),
                    _ => {}
                })
                .build(app)?;
            app.manage(tray);

            if let Some(window) = app.get_webview_window("main") {
                let _ = window.set_always_on_top(true);
                let _ = window.set_skip_taskbar(true);
                let _ = window.show();
                let window_for_event = window.clone();
                window.on_window_event(move |event| {
                    if let WindowEvent::CloseRequested { api, .. } = event {
                        api.prevent_close();
                        let _ = window_for_event.hide();
                    }
                });
            }
            spawn_hid_listener(app.handle().clone());
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
