# Wi-FEye

A handheld Wi-Fi survey tool built on an ESP32-S3 (N16R3), with a touchscreen
UI, SD/flash logging, and three survey modes plus a live monitor.

## Hardware

- ESP32-S3 N16R3
- ILI9341 TFT display + XPT2046 resistive touch
- microSD card (with FFat flash fallback if no card is present)
- Buttons, status LEDs, piezo buzzer

## Modes

1. **Floor-plan tap survey** — load a floor-plan JPEG, tap a point on it to
   log an RSSI reading there.
2. **Walk / zone survey** — walk through named zones, logging RSSI
   continuously per zone for a before/after or room-by-room comparison.
3. **Continuous monitor** — connects to your Wi-Fi via SoftAP provisioning
   (the "ESP SoftAP Prov" phone app), then logs connection events, periodic
   RSSI samples, disconnect reasons, nearby-AP snapshots, and a 2.4GHz
   channel congestion survey over time. Also serves a small web page over
   HTTP so you can browse, download, and delete the CSV logs it's written,
   without pulling the SD card.

A "More" menu adds Diagnostics (heap/uptime) and the File Browser.

## Building

Needs the Arduino IDE (or arduino-cli) with the ESP32 board package, plus:
`WiFiProv`, `WebServer`, `Adafruit_GFX`, `Adafruit_ILI9341`,
`XPT2046_Touchscreen`, `TJpg_Decoder` — all available via Library Manager.

No per-build configuration needed — just open the `.ino` file and flash it.
Modes 1 and 2 (which scan for a network's signal while you walk around)
let you tap which nearby network to survey each time you enter them, from
an on-screen list built from a live scan — no network name is ever
hardcoded. Mode 3's continuous monitor connects via SoftAP provisioning
instead, with the password entered once on your phone (the "ESP SoftAP
Prov" app) and stored only in the ESP32's own flash, never in source.

## Reading the logs

The companion **[Wi-FEye Report](https://claude.ai/artifact/KRiTqe5dQgXiZQ5ptufPwn)**
tool reads the CSV logs straight in your browser (nothing is uploaded
anywhere) and renders RSSI traces, zone comparisons, floor-plan plots,
channel-congestion charts, and a summary conclusion. Export to PDF or a
plain-text summary from there too.

## CSV formats

- **Monitor (Mode 3):** `sequence,storage,timestamp,event,rssi_dbm,recommendation`
- **Walk (Mode 2):** `sequence,storage,event,zone,timestamp,rssi_dbm,avg_rssi_dbm,recommendation,target_ssid`
- **Survey (Mode 1):** `sequence,storage,floorplan,x_percent,y_percent,img_x,img_y,timestamp,rssi_dbm,target_ssid`
- **Boot log (`/boot_log.csv`, every boot, any mode):** `sequence,storage,timestamp,reset_reason_code,reset_reason_label`

## Status

Actively field-tested; see the changelog comment block at the top of the
`.ino` for the build-by-build history.
