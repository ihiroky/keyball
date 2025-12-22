# ZQ Tauri HID Logger (minimal sample)

- Opens a HID device via `hidapi`, logs raw reports, and emits a Tauri event `zq://hid/layer_state` when the report matches the Keyball/ZQ layer format.
- Static frontend lives in `dist/` (no bundler needed); logs appear in the terminal running the app.
- The WebView subscribes to `zq://hid/layer_state` and shows a small overlay near your cursor with the active layer and mask (fades out automatically).
- Built with **Tauri v2**.

## Requirements
### For Ubuntu 24.04
```
 cargo install tauri-cli
 sudo apt install librust-gdk-pixbuf-sys-dev
 sudo apt install librust-gdk-sys-dev
 sudo apt install librust-pango-sys-dev
 sudo apt install librust-cairo-sys-rs-dev
 sudo apt install libatk1.0-dev
 sudo apt install libwebkit2gtk-4.1-dev
 sudo apt install libudev-dev
```

## How to run
1. Set `VENDOR_ID` / `PRODUCT_ID` in `src-tauri/src/main.rs` to your keyboard's VID/PID.
2. From `zq/tauri-hid-sample/src-tauri`, run (Tauri v2 CLI):
   ```bash
   cargo tauri dev
   ```
   or, for a simple run without the CLI wrapper:
   ```bash
   cargo run
   ```
   (Requires Rust + `hidapi` build deps on your OS; no npm install needed for this minimal sample.)

## Report format expected
- `report[0] = 0x01` (ZQ application id)
- `report[1] = 0x01` (protocol version)
- `report[2] = 0x00` (layer state report type)
- `report[3] = highest active layer`
- `report[4] = layer bitmask for layers 0–7`
