/*
  Stage 6: WiFi survey — mode-select menu + Mode 2 (walk, no floor plan)
  ------------------------------------------------------------------------
  Builds on Stage 5. Adds a startup menu with 3 modes:

    Mode 1 — Floor plan survey (everything built in stages 1-5, unchanged):
      tap a spot on a floor plan to log a survey point there.

    Mode 2 — Walk, no floor plan (new this stage): a full-screen traffic
      light (green/amber/red) shows live signal quality, the piezo beeps
      faster as signal gets worse (silent when strong), a physical button
      logs a zone/room change, and every reading is appended to SD with a
      plain-English recommendation.

    Mode 3 — Leave-in-place monitor: connects to the target network (the
      only mode that actually associates rather than just scanning), so
      it can detect real disconnect/reconnect events over time, not just
      RSSI drift. The Wi-Fi password is entered once via the ESP32's
      built-in SoftAP provisioning (the "ESP SoftAP Prov" phone app) and
      stored on-device — never hardcoded. Logs both periodic RSSI
      samples and discrete connect/disconnect events to /monitor_log.csv.
      Blue held when Mode 3 is first entered after a power-up/reset
      wipes any stored credentials and re-opens setup; Green returns to
      the mode menu (and disconnects cleanly).

  Mode 1 controls (revised this stage — logging is now button-driven,
  not touch-driven):
    - Right-edge column (R / - / +): reset view / zoom out / zoom in
    - Touch and drag (elsewhere):    pan around the image; a fixed red
                                      crosshair stays at screen center
                                      showing exactly where a point will
                                      be logged
    - Red (GPIO16):                  log a survey point at the crosshair
                                      (screen center) — replaces the old
                                      quick-tap-to-log gesture, which was
                                      a recurring source of touch-accuracy
                                      trouble (edge/corner inaccuracy,
                                      tap-vs-drag misfires)
    - Yellow (GPIO15):               undo the last logged point
                                      (removes it from screen and CSV)
    - Blue (GPIO17):                 clear all logged points from screen
                                      (CSV file on SD is untouched)
    - Green (GPIO7):                 back to the floor plan picker,
                                      choose a different plan without
                                      rebooting

  Mode 2 controls (new):
    - Blue (GPIO17):   log a zone/room change (increments the zone number
                        shown on screen; every reading from here on is
                        tagged with the new zone)
    - Yellow (GPIO15): mute/unmute the piezo
    - Green (GPIO7):   back to the mode-select menu

  Library needed: TJpg_Decoder (Bodmer). WiFi.h and SD.h are built
  into the ESP32 Arduino core.

  --- Build 2 additions (RTC + NTP timestamps, auto-incrementing logs) ---
  Needs RTClib (Adafruit) in addition to the above. A DS3231 RTC module
  (SDA/SCL on GPIO21/47 -- the pins freed up when the MPU6050 was removed)
  is now the authoritative source of wall-clock timestamps for every CSV
  log (replacing the old seconds-since-boot values), kept accurate across
  power cycles by its own coin-cell backup. Mode 3 opportunistically syncs
  it from NTP (in UTC) whenever it gets a fresh connection. Every Mode 2/3
  log file is now auto-incrementing (e.g. /monitor_log_001.csv,
  /monitor_log_002.csv, ...), generated once at boot, so each power-on
  session gets its own file instead of always appending to one fixed name.

  --- Build 2.1 additions (file-access web server, Mode 3 polish) ---
  Mode 3 now runs a tiny web server (WebServer.h, built into the ESP32
  core -- no extra library needed) once connected: browse to the device's
  IP from any phone/laptop on the same network to list and download every
  file on the SD card and the internal-flash fallback, without pulling the
  card out. No authentication -- trusted-home-network use only. Press
  Yellow in Mode 3 to see the address on-screen (it's no longer on the
  main status screen, to keep that one uncluttered). Mode 3's status text
  is now size-2 (was size-1) for readability. New: a piezo alert beeps
  continuously on a genuine disconnect, silenced by any button press or by
  reconnecting on its own; toggle it on/off with Red (on by default).

  --- Build 7 additions (state consolidation, non-blocking Mode 3 screens,
  disconnect diagnostics, CSV improvements, More menu) ---
  - AppState: the scattered "what's the system doing right now" globals
    (mode, SD/RTC availability, Mode 1 pan/zoom, Mode 2 zone/mute, the
    Mode 1 async-scan flag) are now fields of one `app` struct instead of
    loose globals of the same name.
  - Mode 3's connection state is now a 4-value enum (Mode3ConnectionState:
    OFFLINE/CONNECTING/CONNECTED/RECONNECTING) instead of a plain
    connected/not-connected bool, so a brief drop-and-reconnect (logged as
    RECONNECTED) reads differently from a genuine sustained loss (no
    reconnect within MODE3_RECONNECT_GRACE_MS -- logged as its own
    CONNECTION_LOST event and only then does the status screen fall back
    to its original "waiting" wording).
  - Mode 3's Help and File-access screens no longer block mode3Loop() --
    they're now non-blocking overlays, so WiFi events, periodic logging,
    the congestion scan and the file-access web server all keep running
    while either is shown (see mode3HelpOverlayActive/
    mode3FileAccessOverlayActive on `app`). Modes 1/2's Help screen is
    unchanged (still blocking -- neither has a live connection to babysit).
  - Every WiFi disconnect in Mode 3 now logs the driver's numeric reason
    code plus a decoded short string (wifiDisconnectReasonToString()).
  - The old "possible non-WiFi interference" wording is softened to list
    router-side/compatibility issues alongside it, not lead with it.
    mode3CongestedCount/congestedCount are renamed to
    mode3NearbyApCount/nearbyApCount throughout. The periodic congestion
    scan now also logs up to the 5 strongest same-channel neighbor APs
    (SSID/BSSID/RSSI/channel, hidden SSIDs shown as "<hidden>") as their
    own NEARBY_APS row, and the currently-connected AP's own SSID/BSSID is
    logged once per connection on the CONNECTED/RECONNECTED row.
  - CSV changes: every log file (Mode 1 survey, Mode 2 walk, Mode 3
    monitor, and their FFat fallbacks) now starts each row with a shared,
    ever-incrementing `sequence` number and a `storage` column (SD/FFAT),
    so a gap or a storage flip is visible later. Mode 2 gained an `event`
    column (SAMPLE vs. the new ZONE_CHANGE row). Mode 3 gained a TIME_SYNC
    row on every successful NTP correction. Mode 1 gained img_x/img_y
    pixel-coordinate columns alongside the existing percentage columns.
  - The file-access web server now only lists/serves .csv/.txt files.
  - New top-level "More" menu (alongside Modes 1-3): Diagnostics (heap/
    PSRAM/SD/RTC/WiFi status, firmware version, uptime, last disconnect
    reason) and a minimal File Browser screen pointing at Mode 3's web
    server.
  - NOT done this build: the planned split into multiple files
    (app_state.h/touch.cpp/display.cpp/mode1-3.cpp/storage.cpp/wifi.cpp/
    rtc.cpp) was judged too risky with no compiler available to catch a
    mistake in the cross-file `extern`/prototype wiring, and was skipped
    in favor of a correct single file -- see the implementation report
    for what a follow-up session would need to do it safely.

  Build 7.1 additions (full-channel survey, for the reporting tool):
  - The congestion scan (every MODE3_CONGESTION_CHECK_INTERVAL_MS while
    connected) now tallies ALL channels 1-13, not just the one Mode 3 is
    on, via buildChannelSurvey() -- logged as its own CHANNEL_SURVEY row
    (reusing the existing monitor-log CSV columns, no schema change),
    recommending the quietest of the three non-overlapping channels
    (1/6/11) by visible-AP count. Also shown on the Diagnostics screen.
    Same honesty caveat as the existing congestion note: this counts
    other visible Wi-Fi networks only -- not airtime/traffic utilization,
    and the ESP32-S3's radio is 2.4GHz only, so a 5GHz network is
    invisible to this scan even if it's the same router.
  - Fixed the SoftAP provisioning SSID never broadcasting at Mode 3 entry:
    Serial logs showed "wifi:sta is connecting, cannot set config" every
    time Mode 3 started, coming from a WiFi.begin() call sitting right
    before WiFiProv.beginProvision() in startMode3() -- the STA radio was
    still mid-connect when beginProvision() tried to configure the driver
    to bring up the SoftAP, and that config call was being rejected.
    Removed the WiFi.begin() call; beginProvision() already connects with
    stored credentials on its own when provisioned, so it wasn't doing
    anything useful there anyway.
  - Fixed the same "cannot set config" error recurring during Blue-hold
    "forget network" (forgetMode3NetworkAndRestart()), which was silently
    preventing the credential erase from actually happening -- the SSID
    kept changing after a forget+reboot because the old credentials were
    never really cleared. A first attempt (disconnect(false,false) then a
    300ms delay before the real erase) did not hold, because
    WiFi.setAutoReconnect(true) keeps retrying the stored credentials in
    the background and can put the STA straight back into "connecting"
    before the erase call runs. Fixed by disabling autoReconnect first,
    then fully power-cycling the STA driver (WiFi.mode(WIFI_OFF) then
    back to WIFI_STA) before the erase, so the driver can't still be mid
    some other operation when the erase call runs.
  - Fixed the boot banner still printing "=== WiFi survey (Stage 6) ==="
    (a leftover from before this file was renamed/rebuilt as Build 7) --
    it now prints the real FIRMWARE_VERSION instead, bumped to "7.1".

  Build 7.2 additions (File Browser is now a real page, not an instruction
  screen; file deletion):
  - The More menu's "File Browser" used to just tell you to go start
    Mode 3, connect, then check its Yellow-button screen for the address.
    It now connects itself (reusing whatever credentials are already
    stored -- same ones Mode 3 uses) and runs the same file-access web
    server directly, showing the http:// address right there on screen.
    It does NOT run its own SoftAP/provisioning flow -- if nothing's been
    provisioned yet (or the stored network's out of range) it times out
    after 15s and says so; provisioning is still done via Mode 3.
  - Added a Delete button next to every file on the web page (POST
    /delete, with a JS confirm() so a stray tap/prefetch can't trigger
    it), since there was previously no way to clear old logs off the
    device without pulling the SD card. Same .csv/.txt-only and no-".."
    checks as the existing download path; bumped FIRMWARE_VERSION to
    "7.2".

  Build 7.3 additions (the per-sample recommendation now names a
  specific channel, not just "try switching channel"):
  - Comparing a pre-7.1 monitor log against a 7.1+ one side by side
    showed a gap: the full channel survey (CHANNEL_SURVEY row, Build 7.1)
    already works out a specific best channel among 1/6/11, and the
    Diagnostics screen already shows it -- but the recommendation text
    written into every SAMPLE row still only said the old generic
    "...try switching channel", never naming which one, even moments
    after a CHANNEL_SURVEY row had just worked it out. A report built
    from the log never actually saw the useful answer.
  - buildRecommendation() now takes the current mode3RecommendedChannel
    and, when it differs from the channel actually in use, appends it:
    "...try switching channel (channel 11 currently recommended)."
    Mode 2 has no full channel survey to draw on (it only ever scans the
    hardcoded TARGET_SSID's own channel), so its call site just passes 0
    and keeps the old generic wording. Bumped FIRMWARE_VERSION to "7.3".

  Build 7.5 additions (robustness pass, from an external code review --
  no new features, per that review's own framing and the brief for this
  build):
  - Channel tie-break fix: buildChannelSurvey() used to always start its
    comparison from channel 1, so a 3-way tie between 1/6/11 always
    "recommended" switching to 1 even if you were already sitting on an
    equally-quiet 6 or 11 -- advice with no real benefit behind it. It now
    starts from whichever of the three you're already on and only moves
    away when another is STRICTLY quieter.
  - Heap-health logging: checkMode3HeapHealth(), checked on the same
    cadence as the channel-congestion scan, prints free heap to Serial
    every time and logs a LOW_HEAP warning row if it ever drops below
    MODE3_LOW_HEAP_THRESHOLD_BYTES (40000). The Diagnostics screen already
    showed a live "Heap free" reading (Build 7) -- this adds the logged,
    thresholded side, turning "heavy String usage could fragment the heap
    over days/weeks" from a theoretical worry into something observable
    in the field.
  - html.reserve(4096) in handleFileListPage() -- the one place in the
    sketch building a non-trivial String through repeated concatenation.
    Deliberately NOT a wider String-to-buffer rewrite: that page is
    requested rarely (opening the File Browser, or Mode 3's Yellow
    button), not something running in a tight loop, so this one line
    covers the actual exposure without the risk of a much larger change.
  - SysProvEvent()/mode3Loop() event-ordering fix: SysProvEvent() runs on
    its own FreeRTOS task and used to just set app.mode3State plus a
    single "something changed" flag (mode3NeedsRedraw) -- if a disconnect
    was immediately followed by a reconnect (well within the gap between
    two mode3Loop() iterations during a brief AP blip), the flag only
    ever reflected the FINAL state by the time mode3Loop() checked it, so
    the DISCONNECTED event -- and the drop it represented -- silently
    never got logged. For an instrument whose whole purpose in Mode 3 is
    catching exactly this kind of intermittent behaviour, that's a real
    defect. Fixed with a small single-producer/single-consumer ring
    buffer (Mode3EventEntry/pushMode3Event()/popMode3Event(), guarded by
    a portMUX_TYPE spinlock -- Espressif's documented lightweight way to
    protect data shared with the WiFi event task): every real transition
    is now queued with its own timestamp and drained/logged in order by
    mode3Loop(), so a rapid DISCONNECTED-then-CONNECTED pair produces two
    rows, not zero, and the drop counter/connected-time bookkeeping is no
    longer at the mercy of how fast mode3Loop() happened to notice.
  - Channel-survey-coincidence tagging: paired with the fix above, since
    both are about correctly attributing why/when a disconnect happened.
    Each queued event now also carries whether a channel-survey scan
    (WiFi.scanNetworks(), which briefly takes the radio off the connected
    channel) was in flight at that exact moment -- DISCONNECTED/
    (RE)CONNECTED rows say so when it was, so the log doesn't quietly
    blame the AP for something this device's own periodic scan may have
    caused.
  - Bumped FIRMWARE_VERSION to "7.5".
  - Build 7.6, two scoped fixes shipped together:
    1. Reset-reason logging: after a field crash during Mode 1 (red-button
       tap logged a point, unit fully rebooted) that didn't reproduce when
       reconnected over USB for serial output -- itself a clue, since USB
       supplies steadier power than the battery chain does -- added
       printResetReason(), printed first thing in setup() via
       esp_reset_reason(). This distinguishes a BROWNOUT (supply voltage
       sagged too low, e.g. a WiFi-scan current spike on a marginal/aging
       battery) from a PANIC, a task/interrupt watchdog timeout, or an
       ordinary power-on/software reset -- so the next crash, if there is
       one, says which kind it was instead of just "it rebooted".
    2. Same-radio/multi-SSID grouping (the fix actually agreed on first,
       from reviewing Mode 3 field logs across multiple rooms/APs): this
       site's enterprise APs each broadcast several SSIDs off one
       physical radio (e.g. CFG_Team/CFG_Guest/CFG_RZ/Zorba/Major-Clks all
       sharing one base MAC, confirmed on two separate AP units in two
       different rooms). countMode3NearbyApsOnChannel(),
       buildNearbyApSummary(), and buildChannelSurvey() previously only
       excluded an exact SSID match against the connected network, so a
       single AP broadcasting five SSIDs was counted as five separate
       competing networks -- badly overstating channel congestion and
       sometimes recommending a channel switch with no real benefit
       behind it. New isSiblingRadio() helper matches the first 5 BSSID
       octets (vendor+device prefix) against the connected AP's own BSSID
       and excludes those too, in both the "X other networks on this
       channel" count and the CHANNEL_SURVEY per-channel tally. Scoped to
       Mode 3 (the only mode that actually connects, and the only one
       with logged NEARBY_APS/CHANNEL_SURVEY evidence of the problem) --
       Mode 2's analogous countNearbyApsOnChannel() has the same
       exact-SSID-only gap but was left untouched here, flagged as a
       candidate for later rather than widened without its own evidence.
    Bumped FIRMWARE_VERSION to "7.6".
  - Build 8: on-screen target-network picker for Modes 1/2. Until now,
    TARGET_SSID (secrets.h) was the ONLY network Modes 1/2 could ever
    survey, requiring a recompile+reflash every time this device moved to
    a site with a different network name -- a real problem now that it's
    being used away from home (e.g. a food manufacturer's site). Since
    Modes 1/2 never actually associate with the network (only scan for its
    beacon and read its RSSI), there's no need for the target to be fixed
    at compile time. Added activeTargetSsid, chosen at runtime from a
    tap-to-pick list built from a live scan (scanNetworksForPicker(),
    drawNetworkList(), selectTargetNetwork()), wired into enterMode() so
    it's prompted once per fresh entry into Mode 1 or 2 from the mode
    menu (not on every Green-button floor-plan switch within Mode 1,
    which stays on the same network as before). Lists each visible
    non-hidden SSID once (deduped, strongest-signal reading kept),
    strongest-first, with RSSI shown per row; Green at the picker backs
    out to the mode menu, same convention as the floor-plan and mode
    pickers. TARGET_SSID itself is unchanged and still used as the
    compiled-in default shown at boot. Bumped FIRMWARE_VERSION to "8.0".
  - Build 8.1: a battery-powered field crash during Mode 2 testing proved
    Build 7.6's printResetReason() wasn't actually enough on its own --
    it only ever printed to Serial, which goes nowhere when the crash
    happens untethered on battery with no Serial Monitor listening. Added
    logBootEvent(), called once in setup() right after SD/FFat and the
    RTC are both ready (so it has somewhere to write and a real
    timestamp), writing the SAME reset-reason information to a new,
    dedicated /boot_log.csv instead (sequence,storage,timestamp,
    reset_reason_code,reset_reason_label) -- one row every boot, not just
    crashes, so a pattern of repeated brownouts is visible over time, not
    just a single incident. Deliberately its own file rather than living
    inside any one mode's CSV, since a crash can happen during Mode 1 or
    2 testing just as easily as Mode 3, and this is written before any
    mode is even chosen. Uses the same live SD-preferred/internal-flash-
    fallback choice as Mode 2/3's own loggers. resetReasonLabel() pulled
    out of printResetReason() so both it and logBootEvent() share the
    exact same wording rather than keeping two copies in sync by hand.
    Bumped FIRMWARE_VERSION to "8.1".
  - Build 8.2: removed secrets.h/TARGET_SSID entirely. Mode 3 never used
    it (SoftAP Prov instead), and once Build 8 added the on-screen
    network picker, Modes 1/2 stopped needing a compiled-in default too --
    enterMode() always sets activeTargetSsid from a real on-screen choice
    before either mode runs, so the old default had quietly become dead
    weight (noticed when asked why a secrets file was still needed at
    all, given neither mode can actually reach it anymore). Dropped the
    #include, changed activeTargetSsid's initial value to "", removed the
    boot banner's "Default target SSID" line, deleted secrets.h.example,
    and removed the secrets.h entry from .gitignore. No per-build
    configuration is needed anymore -- the sketch just compiles and
    flashes as-is. Bumped FIRMWARE_VERSION to "8.2".
*/

#include <SPI.h>
#include <SD.h>
#include <FFat.h>
#include <Wire.h>
#include <RTClib.h>      // DS3231 real-time clock (Build 2) -- needs Wire.h above
#include <WiFi.h>
#include <WiFiProv.h>
#include <esp_system.h>  // esp_reset_reason() -- logs WHY the board last restarted (see printResetReason())
#include <WebServer.h>   // simple file-access web server for Mode 3 (built into the ESP32 core, no extra library needed)
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <TJpg_Decoder.h>
#include "wifeye_logo_data.h"   // Wi-FEye boot logo, baked into flash (see showBootScreen)
// Build 8.2: secrets.h/TARGET_SSID removed -- Mode 3 never used it (SoftAP
// Prov instead), and Build 8's on-screen network picker means Modes 1/2
// no longer need a compiled-in default either (enterMode() always picks a
// real target before entering either mode, so the old default was already
// dead weight). Nothing here needs to be set per-build/per-person anymore,
// so there's no more "secrets" file at all.

// Forward declaration, needed only because of how Arduino compiles a .ino:
// it auto-generates a prototype for every function in the sketch and
// inserts them all right here, immediately after the #include block --
// long before Mode3EventEntry's own definition further down (see
// pushMode3Event()/popMode3Event(), which take it by reference). Without
// this, those auto-generated prototypes reference a type that doesn't
// exist yet at this point in the file, and the sketch fails to compile
// with "'Mode3EventEntry' does not name a type". A plain forward
// declaration is enough here since the auto-generated prototypes only
// need a reference to the type, not its full definition.
struct Mode3EventEntry;

// Build 7: shown on the new Diagnostics screen (More menu) and worth
// bumping by hand whenever a build is flashed, same spirit as the
// Build 1/2/2.1/... notes in the big comment block above.
#define FIRMWARE_VERSION "8.2"

// RSSI thresholds (dBm):
//   >= -60 dBm : good / green
//   -60 to -75 : usable but weak / amber
//   <  -75 dBm : poor / red
const int RSSI_GREEN_THRESHOLD = -60;
const int RSSI_AMBER_THRESHOLD = -75;
const int RSSI_NOT_FOUND = 1;   // sentinel — real RSSI values are always negative
const int RSSI_PENDING = 2;     // sentinel — scan in progress, not a real RSSI value

// TFT — confirmed working pins
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8
#define TFT_MOSI 11
#define TFT_MISO 13
#define TFT_CLK  12

// Touch — confirmed working pins
#define T_CS   18
#define T_IRQ  38

// SD card (shares bus)
#define SD_CS 39

// RTC (DS3231) -- I2C, Build 2. Shares the bus the MPU6050 used before it
// was physically removed from the project; nothing else is on this bus now.
#define RTC_SDA 21
#define RTC_SCL 47

// Buttons — Mode 1: red=log point at crosshair, yellow=undo, blue=clear screen, green=back to picker
//           Mode 2: yellow=mute piezo, green=back to mode menu, blue=zone change
//           Mode 3: yellow=show file-access address, red=toggle disconnect-alert piezo,
//                   blue (hold 2s)=forget network, green=back to mode menu
#define BTN_YELLOW 15
#define BTN_BLUE   17
#define BTN_GREEN   7
#define BTN_RED    16   // clear survey points from screen (Mode 1 only)
// Same in all 3 modes: press to show an on-screen help/button-reference
// screen, press again to return to whatever was on screen before. GPIO5
// was freed up when the two status LEDs were removed from the circuit —
// change this if a different physical button ends up wired there instead.
#define BTN_HELP    5

#define PIEZO 4

// Waits for a button to read HIGH (released) before continuing, but never
// forever — if the pin is stuck LOW (a marginal connection, a jostled wire,
// noise) this still gives up after BUTTON_RELEASE_TIMEOUT_MS and carries on,
// rather than freezing the whole sketch with no further Serial output. A
// bare `while (digitalRead(pin) == LOW) delay(10);` has no such escape.
const unsigned long BUTTON_RELEASE_TIMEOUT_MS = 2000;
void waitForRelease(uint8_t pin) {
  unsigned long start = millis();
  while (digitalRead(pin) == LOW) {
    if (millis() - start > BUTTON_RELEASE_TIMEOUT_MS) {
      Serial.println("Warning: button pin still reads LOW after timeout — check wiring/connection. Continuing anyway.");
      break;
    }
    delay(10);
  }
}

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen touch(T_CS, T_IRQ);

// RTC (Build 2) -- authoritative wall-clock time, kept accurate across power
// cycles (and even with no internet at all) by its own coin-cell backup.
// Synced opportunistically from NTP whenever Mode 3 has a live connection
// (see syncRtcFromNtp()) rather than depending on NTP alone.
RTC_DS3231 rtc;

// ===========================================================================
// AppState — Build 7: consolidates the scattered "what is the system doing
// right now" globals that used to be read from many functions (mode,
// storage/RTC availability, Mode 1 pan/zoom, Mode 2 zone/mute, the Mode 1
// async-scan flag) into one struct, plus the new Mode 3 connection state
// machine. Globals that are genuinely local to one function/mode (Mode 3's
// many millis()-timestamp/counter fields that only startMode3()/mode3Loop()
// touch, Mode 2's RSSI history ring buffer, etc.) are deliberately left as
// they were -- folding every single global in here would just move the
// clutter around without making it any clearer what's actually shared,
// cross-function state.
// ===========================================================================
enum Mode3ConnectionState {
  MODE3_OFFLINE,       // not connected and not currently retrying (before the very first connect attempt, or after a sustained loss gives up retrying for now -- see MODE3_RECONNECT_GRACE_MS)
  MODE3_CONNECTING,    // WiFi.begin()/SoftAP provisioning in progress. Covers both "never provisioned yet, waiting for the phone app" and "provisioned, trying to associate" as ONE state rather than splitting it into a 5th on-screen case -- the status screen's existing "Waiting for setup..." wording already covered both as one bucket, so this build keeps that instead of inventing new wording for a distinction nobody asked for.
  MODE3_CONNECTED,     // got an IP, currently associated
  MODE3_RECONNECTING   // was connected, just disconnected; auto-reconnect gets a short grace window (MODE3_RECONNECT_GRACE_MS) before this is demoted to MODE3_OFFLINE and logged as a genuine sustained loss
};

struct AppState {
  int currentMode = 0;              // 0 = not yet chosen; set once in setup() via selectMode()
  bool sdCardAvailable = false;
  bool rtcAvailable = false;

  // Mode 1 (floor plan survey) pan/zoom + async-scan state
  float zoom = 1.0;
  float centerX = 0, centerY = 0;
  bool scanPending = false;

  // Mode 2 (walk survey)
  int currentZone = 1;
  bool piezoMuted = false;

  // Mode 3 (leave-in-place monitor)
  Mode3ConnectionState mode3State = MODE3_OFFLINE;
  uint8_t mode3LastDisconnectReason = 0;        // raw wifi_err_reason_t code from the most recent disconnect (Build 7 item 3)
  bool mode3HaveLastDisconnectReason = false;   // false until a disconnect has actually been logged this boot

  // Non-blocking Mode 3 Help/File-access overlays (Build 7 item 2)
  bool mode3HelpOverlayActive = false;
  unsigned long mode3HelpOverlayShownMs = 0;
  bool mode3FileAccessOverlayActive = false;
  unsigned long mode3FileAccessOverlayShownMs = 0;
};
AppState app;

// Build 7 item 5: one counter shared by EVERY CSV log function in the
// sketch (Mode 1's survey log, Mode 2's walk log, Mode 3's monitor log,
// and all of their SD/FFat fallback variants) -- incremented right before
// a row is written and recorded as that row's first column, so a gap in
// the sequence is visible later even if storage flips between SD and
// internal flash mid-session, which a per-file row count alone wouldn't
// catch. Deliberately global/shared rather than per-file: the whole point
// is one continuous count regardless of which log or which filesystem a
// given row ended up on.
unsigned long globalLogSequence = 0;

// Confirmed touch calibration
const int X_raw_min = 842;
const int X_raw_max = 3300;
const int Y_raw_min = 760;
const int Y_raw_max = 3249;

const int SCREEN_W = 320;
const int SCREEN_H = 240;

// Set in setup() after the SD retry loop; checked before anything SD-based
// is attempted (currently just pickAndLoadFloorPlan() / Mode 1) so a
// missing card disables Mode 1 cleanly instead of leaving the whole device
// stuck (that used to `return` out of setup() entirely before Mode 3 had
// its own FFat-based fallback and no longer needed SD to function at all).
// (Build 7: this is now app.sdCardAvailable -- see the AppState struct.)

// A single, quick SD.begin() attempt — no retry loop (that's only needed
// once at boot, for the card to finish powering up/settling; by the time
// this runs mid-session the SPI bus is already up and running). Used both
// by setup()'s boot-time retry loop and, mid-session, to notice a card
// that's been inserted after boot or re-check one that just failed a
// write (it may simply have been removed).
bool trySDInit() {
  if (SD.begin(SD_CS, SPI, 4000000)) return true;
  SD.end();
  return false;
}

// Build 2: finds the next unused filename like "/monitor_log_003.csv" on the
// given filesystem, starting from 001, so each power-on session gets its
// own log file instead of always appending to the same fixed name -- makes
// it trivial to tell sessions apart later. Falls back to the plain
// "<prefix><ext>" name if somehow all 999 slots are taken (never expected
// in practice, but safer than looping forever).
void nextAvailableFilename(fs::FS &fs, const char *prefix, const char *ext, char *outBuf, size_t outBufSize) {
  for (int n = 1; n <= 999; n++) {
    snprintf(outBuf, outBufSize, "%s_%03d%s", prefix, n, ext);
    if (!fs.exists(outBuf)) return;
  }
  snprintf(outBuf, outBufSize, "%s%s", prefix, ext);
}

// --- Storage preference for Mode 2/3 LOGGING (not Mode 1, which always
// needs SD for its floor plan JPEGs regardless of this choice) ---
// false = prefer SD (default); true = prefer internal flash. Set once at
// boot by chooseStoragePreference(), below.
bool preferInternalLogging = false;
const unsigned long STORAGE_CHOICE_TIMEOUT_MS = 8000;

// Shown once at boot, right after the logo, so Mode 2/3 logging has a
// deliberate, predictable storage choice instead of only ever
// auto-detecting. Button-driven rather than touch -- simpler, and matches
// the project's other boot-time button conventions (e.g. hold Blue in
// Mode 3 to forget a network). Auto-selects the default (SD) after a
// short timeout rather than waiting forever, since Mode 3 is meant to
// also work completely unattended (e.g. after a power cut, with nobody
// there to press anything). Whichever is picked, the automatic SD<->
// internal fallback already built into appendWalkCsvLine()/
// appendMonitorLogLine() still applies underneath when SD is preferred --
// this screen only decides which one is tried FIRST, so a reading is
// never lost either way.
void chooseStoragePreference() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Log storage:");
  tft.setTextSize(1);
  tft.setCursor(10, 50);
  tft.println("Yellow = SD Card (default)");
  tft.setCursor(10, 65);
  tft.println("Green  = Internal Memory");
  tft.setCursor(10, 90);
  tft.println("Either way a reading is never lost --");
  tft.setCursor(10, 102);
  tft.println("the other is used automatically if the");
  tft.setCursor(10, 114);
  tft.println("one you pick isn't available.");
  tft.setCursor(10, SCREEN_H - 15);
  tft.println("No press: defaults to SD in a few seconds.");

  unsigned long waitStart = millis();
  while (millis() - waitStart < STORAGE_CHOICE_TIMEOUT_MS) {
    if (digitalRead(BTN_YELLOW) == LOW) {
      preferInternalLogging = false;
      waitForRelease(BTN_YELLOW);
      Serial.println("Log storage: SD Card preferred (chosen at boot)");
      return;
    }
    if (digitalRead(BTN_GREEN) == LOW) {
      preferInternalLogging = true;
      waitForRelease(BTN_GREEN);
      Serial.println("Log storage: Internal Memory preferred (chosen at boot)");
      return;
    }
    delay(20);
  }
  preferInternalLogging = false;
  Serial.println("Log storage: no button pressed within the timeout, defaulting to SD Card preferred");
}

// Chosen at runtime from the picker screen (see scanFloorPlans/selectFloorplan)
char FLOORPLAN_FILE[40] = "";
// Derived from FLOORPLAN_FILE once it's chosen — see deriveCsvFilename()
char CSV_FILE[40] = "";

// --- Floor plan picker ---
#define MAX_FLOORPLANS 8   // how many fit on screen at once, see selectFloorplan()
char floorplanNames[MAX_FLOORPLANS][40];
int floorplanCount = 0;
const int LIST_TOP = 30;
int ROW_H = 30;   // recomputed to fill the screen once floorplanCount is known — see scanFloorPlans()

// --- Build 8: on-screen target-network picker for Modes 1/2 ---
// A hardcoded TARGET_SSID used to be the ONLY network Modes 1/2 could ever
// survey — fine for a single site, but it meant a recompile+reflash every
// time this device moved to a different location with a different network
// name (e.g. home's "Poohcorner2" vs a site's "Guest123"). Since Modes 1/2
// never actually associate with the network (they only scan for its beacon
// and read its RSSI), there's no real need to hardcode it at all:
// activeTargetSsid is chosen at runtime instead, from a tap-to-pick list
// built from a live scan — see scanNetworksForPicker()/selectTargetNetwork()
// below, wired up in enterMode(), which always sets this to a real choice
// before Mode 1/2 ever runs. Starts empty -- Build 8.2 removed the old
// compiled-in default (secrets.h/TARGET_SSID) once it became clear nothing
// ever actually used it anymore.
String activeTargetSsid = "";
#define MAX_SURVEY_NETWORKS 8   // how many fit on screen at once, same idea as MAX_FLOORPLANS
char surveyNetworkNames[MAX_SURVEY_NETWORKS][33];   // SSIDs are at most 32 chars + null terminator
int surveyNetworkRssi[MAX_SURVEY_NETWORKS];
int surveyNetworkCount = 0;
const int NET_LIST_TOP = 30;
int netRowH = 30;   // recomputed to fill the screen once surveyNetworkCount is known — see scanNetworksForPicker()

// --- On-screen zoom controls: a tall column down the right edge (R / - / +
// stacked top to bottom), rather than three narrow squares crammed into a
// corner. The screen edges are where a simple 2-point touch calibration is
// least accurate, so cramping 3 small targets into a 90px-wide strip right
// at that edge meant taps regularly landed in the wrong one. Full-height
// bands are both bigger AND spread across a wider raw-coordinate range,
// the same fix that made the floor plan picker rows reliable.
const int TOOLBAR_W = 54;
const int TOOLBAR_X = SCREEN_W - TOOLBAR_W;     // 266
const int BAND_H = SCREEN_H / 3;                // 80 — R / - / + each get a full-height third

// Hit-test extends a little left of the drawn column, and to the true
// top/bottom/right screen edges, so an edge-clamped touch is still caught.
const int ZOOM_HIT_LEFT = TOOLBAR_X - 4;   // 262

// --- Image buffer (allocated in PSRAM once we know the JPEG's size) ---
uint16_t *imgBuffer = nullptr;
uint16_t imgW = 0, imgH = 0;

// --- View state --- (zoom/app.centerX/app.centerY now live in app -- see AppState)
float minZoom = 1.0;
float maxZoom = 4.0;

bool needsRedraw = true;
unsigned long lastRenderMs = 0;

// --- Mode select (0 = not yet chosen; set once in setup() via selectMode()) --- (now app.currentMode)

// --- Mode 2 (walk, no floor plan) state ---
char WALK_CSV_FILE[48] = "/walk_survey_001.csv";   // overwritten with the actual auto-incremented name in setup() (Build 2)
char WALK_CSV_FALLBACK_FILE[48] = "/walk_survey_fallback_001.csv";   // overwritten with the actual auto-incremented name in setup() (Build 2)
// app.currentZone/app.piezoMuted now live in app -- see AppState
unsigned long lastBeepMs = 0;

// Small ring buffer of the last few readings, so the traffic light settles
// instead of flickering between bands on one noisy scan. RSSI_NOT_FOUND
// readings count as a strong-negative stand-in (worse than any real
// reading) so "network not in range at all" still pulls the average down
// into red rather than being averaged away.
#define RSSI_HISTORY_LEN 3
int rssiHistory[RSSI_HISTORY_LEN];
int rssiHistoryCount = 0;
const int RSSI_NOT_FOUND_STANDIN = -100;

bool ambientScanPending = false;
unsigned long ambientScanStartMs = 0;
unsigned long lastAmbientScanStartMs = 0;
const unsigned long AMBIENT_SCAN_INTERVAL_MS = 4000;   // start a new scan this often once the previous one's done
int lastDisplayedBand = -1;   // -1 = not drawn yet; forces the first draw
int lastDisplayedZone = -1;

// Latest smoothed reading, set by checkAmbientScan() and read every loop
// iteration by updatePiezo() — kept as state rather than recomputed each
// time so the piezo and the display always agree on the current reading.
int currentBand = 2;             // 0 = green, 1 = amber, 2 = red
bool currentNotFound = false;
int currentAvgRssi = RSSI_NOT_FOUND_STANDIN;
bool haveAmbientReading = false;   // false until the first scan completes

// --- Mode 3 (leave-in-place monitor) state ---
// PoP ("proof of possession") and the SoftAP name are shown on-screen so
// whoever's setting the device up can find/enter them in the "ESP SoftAP
// Prov" phone app. They don't need to be secret for a personal project.
#define MODE3_POP "abcd1234"
#define MODE3_SERVICE_NAME "PROV_WIFISURVEY"
char MONITOR_LOG_FILE[48] = "/monitor_log_001.csv";   // overwritten with the actual auto-incremented name in setup() (Build 2)
char MONITOR_LOG_FALLBACK_FILE[48] = "/monitor_log_fallback_001.csv";   // overwritten with the actual auto-incremented name in setup() (Build 2)
const unsigned long MODE3_SAMPLE_INTERVAL_MS = 30000;   // periodic RSSI sample while connected

// File-access web server -- reachable at http://<device-ip>/ once Mode 3
// has a live connection (see drawMode3Status() for the on-screen address).
// No authentication: this is meant for use on your own trusted home
// network only, same spirit as the open SoftAP Prov setup above. Routes
// are registered once in setup(); handleClient() is pumped from
// mode3Loop() every iteration -- it's a no-op the rest of the time since
// there's no connection for anyone to reach it through.
WebServer fileServer(80);

// Disconnect alert (piezo) -- enabled by default, toggled with Red while in
// Mode 3. mode3AlertActive is true from the moment a genuine disconnect is
// logged until either a button is pressed (anywhere in Mode 3) or the
// device reconnects on its own -- see mode3Loop()/updateMode3Alert().
bool mode3AlertPiezoEnabled = true;
bool mode3AlertActive = false;

bool mode3Started = false;      // true once provisioning/connect has been kicked off this boot
// mode3Connected (bool) replaced in Build 7 by app.mode3State (Mode3ConnectionState) -- see AppState
const unsigned long MODE3_RECONNECT_GRACE_MS = 20000;   // Build 7: how long MODE3_RECONNECTING is given before a disconnect that hasn't resolved counts as a genuine sustained loss (see mode3Loop()). Chosen as a middle ground between a brief AP reboot/channel-switch blip (often resolves in a few seconds) and a real outage -- long enough not to cry wolf on the former, short enough not to sit "reconnecting" for minutes on the latter.
unsigned long mode3ReconnectingSinceMs = 0;             // millis() when MODE3_RECONNECTING began; checked against the grace period above
String mode3NearbyApSummary = "";            // most recent nearby-same-channel-AP summary built by checkMode3CongestionScan() (Build 7 item 4), logged as its own NEARBY_APS row rather than bloating every SAMPLE row
unsigned long mode3LastSampleMs = 0;
bool mode3NeedsNtpSync = false;   // Build 2: set by SysProvEvent() on a fresh connection, actioned in mode3Loop() (NTP sync must not run on SysProvEvent's own FreeRTOS task)
bool mode3NeedsWebServerStart = false;   // Build 2.1: set by SysProvEvent() on a fresh connection, actioned in mode3Loop() -- see fileServer comment in setup()
bool fileServerStarted = false;          // so a reconnect later doesn't try to begin() an already-listening server

// Build 7.5: a code review flagged a real architectural gap -- SysProvEvent()
// (on its own FreeRTOS task) used to just set app.mode3State plus a single
// "something changed, go redraw/log it" flag (the old mode3NeedsRedraw bool
// and mode3LastConnectWasReconnect this replaces). If a disconnect was
// immediately followed by a reconnect -- entirely possible during a brief
// AP blip, well within the time between two mode3Loop() iterations -- the
// flag only ever reflected the FINAL state by the time mode3Loop() checked
// it, so the DISCONNECTED event (and the drop it represented) silently
// never got logged at all. For an instrument whose whole purpose in Mode 3
// is catching exactly this kind of intermittent behaviour, that's a real
// defect, not just a style issue.
//
// Fix: a small single-producer (SysProvEvent, its own task) / single-
// consumer (mode3Loop, the main task) ring buffer. Every real transition
// gets its own queued entry with its own timestamp, instead of collapsing
// into one shared "current state" flag -- mode3Loop() drains and logs
// every entry in order, so a rapid DISCONNECTED-then-CONNECTED pair now
// produces two rows, not zero. portMUX_TYPE (a lightweight ESP32 spinlock,
// Espressif's documented way to protect a small amount of data shared with
// the WiFi event task) guards the few instructions that touch the shared
// head/tail indices -- cheaper than a full FreeRTOS queue for something
// this small, and the queue is sized generously (8) for how rarely real
// transitions happen, not to hold a long history.
enum Mode3EventType : uint8_t {
  MODE3_EVT_DISCONNECTED,
  MODE3_EVT_CONNECTED
};
struct Mode3EventEntry {
  Mode3EventType type;
  uint8_t disconnectReason;   // only meaningful for MODE3_EVT_DISCONNECTED
  bool hadReason;             // only meaningful for MODE3_EVT_DISCONNECTED
  bool wasReconnect;          // only meaningful for MODE3_EVT_CONNECTED -- true if the state just before this was MODE3_RECONNECTING
  bool duringChannelSurvey;   // Build 7.5 item 2: true if a channel-survey scan (WiFi.scanNetworks()) was in flight at the moment this happened -- that scan briefly takes the radio off the connected channel, so a disconnect right then may be the scan's own side effect rather than a genuine AP-side drop
  uint32_t atMs;              // millis() when SysProvEvent() actually saw this, not when mode3Loop() got around to draining it -- more accurate for the connected-time/drop bookkeeping than the old single read-it-when-you-notice-it approach
};
const int MODE3_EVENT_QUEUE_LEN = 8;
Mode3EventEntry mode3EventQueue[MODE3_EVENT_QUEUE_LEN];
volatile int mode3EventQueueHead = 0;   // next slot the producer (SysProvEvent) writes
volatile int mode3EventQueueTail = 0;   // next slot the consumer (mode3Loop) reads
portMUX_TYPE mode3EventQueueMux = portMUX_INITIALIZER_UNLOCKED;

// Pushes one event onto the queue -- called only from SysProvEvent() (the
// producer). If the queue is ever completely full (it would take 8 real
// transitions queueing up between two mode3Loop() iterations, which in
// practice never happens) the oldest unread entry is dropped rather than
// overwriting the producer's own bookkeeping -- logging slightly late
// beats corrupting the ring buffer.
void pushMode3Event(const Mode3EventEntry &entry) {
  portENTER_CRITICAL(&mode3EventQueueMux);
  int nextHead = (mode3EventQueueHead + 1) % MODE3_EVENT_QUEUE_LEN;
  if (nextHead != mode3EventQueueTail) {   // room for it
    mode3EventQueue[mode3EventQueueHead] = entry;
    mode3EventQueueHead = nextHead;
  }
  portEXIT_CRITICAL(&mode3EventQueueMux);
}

// Pops one event for the consumer (mode3Loop). Returns false when the
// queue is empty.
bool popMode3Event(Mode3EventEntry &outEntry) {
  bool got = false;
  portENTER_CRITICAL(&mode3EventQueueMux);
  if (mode3EventQueueTail != mode3EventQueueHead) {
    outEntry = mode3EventQueue[mode3EventQueueTail];
    mode3EventQueueTail = (mode3EventQueueTail + 1) % MODE3_EVENT_QUEUE_LEN;
    got = true;
  }
  portEXIT_CRITICAL(&mode3EventQueueMux);
  return got;
}

// Channel-congestion info for the recommendation logged with each SAMPLE
// row. Getting this needs a WiFi scan, which we'd rather not do every 30s
// on a device left in place purely to watch for real drops — scanning
// while connected is generally fine on ESP32 but is still extra load on
// the radio, so it only runs occasionally (MODE3_CONGESTION_CHECK_INTERVAL_MS)
// and asynchronously (never blocks mode3Loop() or delays a drop being
// noticed). Until the first check completes, nearbyApCount stays 0 and
// buildRecommendation() just omits the congestion clause.
const unsigned long MODE3_CONGESTION_CHECK_INTERVAL_MS = 300000;   // 5 minutes
bool mode3ScanPending = false;
unsigned long mode3LastCongestionCheckMs = 0;
int mode3NearbyApCount = 0;
int mode3CongestionChannel = -1;
int mode3RecommendedChannel = 0;   // Build 7.1: quietest of channels 1/6/11 by visible-AP count, set by buildChannelSurvey() each completed scan; 0 until the first scan completes

// Build 7.5: a code review raised heavy String usage (heap-free bytes
// drifting down over days/weeks of continuous Mode 3 monitoring) as a
// theoretical risk. Rather than guess, this turns it into something
// actually observable -- a live reading on the Diagnostics screen, plus a
// logged warning if it ever actually drops below a conservative "worth
// watching" threshold. Checked on the same cadence as the channel-
// congestion scan (cheap, infrequent) rather than every loop() iteration.
const uint32_t MODE3_LOW_HEAP_THRESHOLD_BYTES = 40000;   // conservative -- the ESP32-S3 N16R3 starts with roughly 300KB+ free; this is meant to flag "worth watching", not "about to crash"
unsigned long mode3LastHeapCheckMs = 0;

// --- Mode 3 running summary stats (Build 1) — reset each time monitoring
// starts (startMode3(), first entry only), surfaced on the Help screen ---
unsigned long mode3MonitorStartMs = 0;     // millis() when this monitoring session began
unsigned long mode3ConnectedAccumMs = 0;   // total time spent connected so far, not counting the current span
unsigned long mode3LastStateChangeMs = 0;  // millis() at the last genuine connect/disconnect transition
int mode3DropCount = 0;                    // count of real disconnect events (mirrors DISCONNECTED rows logged)
long mode3RssiSum = 0;                     // sum of periodic SAMPLE RSSI readings, for the running average
int mode3RssiSampleCount = 0;
int mode3LastKnownRssi = RSSI_NOT_FOUND;   // most recent RSSI while connected — used by the interference heuristic at disconnect

// Hold Blue for this long while in Mode 3 to forget the stored network and
// restart into a fresh SoftAP Prov setup — see forgetMode3NetworkAndRestart().
const unsigned long BLUE_FORGET_HOLD_MS = 2000;
unsigned long blueHoldStartMs = 0;   // 0 = not currently holding Blue

// --- Async WiFi scan state, checked every loop() iteration ---
// WiFi.scanNetworks() blocks for ~2-3s if called directly, which used to
// freeze touch/pan entirely while a tap's scan ran. Started in "async"
// mode instead: kicked off in logSurveyPoint(), then polled from loop()
// via checkPendingScan() so the rest of the sketch (pan, zoom, buttons)
// keeps responding while it runs in the background. (Now app.scanPending.)
int pendingPointIdx = -1;
float pendingXPercent = 0, pendingYPercent = 0;
float pendingImgX = 0, pendingImgY = 0;   // Build 7: full-resolution pixel coords, carried through to the CSV's img_x/img_y columns alongside the existing percentages
unsigned long scanStartMs = 0;
// Full-screen redraws are the slow part of a drag (a whole-screen SPI
// blast per frame). Capping how often we actually repaint — while still
// updating app.centerX/app.centerY and reading touch every loop iteration — lets
// touch get sampled far more often than the screen can be repainted, so
// a fast drag accumulates smoothly instead of the whole loop being paced
// by the slowest step.
const unsigned long MIN_RENDER_INTERVAL_MS = 40;   // ~25 fps cap

// --- Touch / drag tracking (Mode 1 touch is pan + zoom icons only —
// logging a point is done via the crosshair + red button instead) ---
bool wasTouching = false;
bool touchOnUI = false;   // true if the current touch started on a zoom icon
int16_t lastTouchScreenX = 0, lastTouchScreenY = 0;

// --- Survey points (kept in RAM for on-screen dots; CSV is the durable copy) ---
struct SurveyPoint {
  float imgX, imgY;   // full-resolution image pixel coordinates
  int rssi;
  uint16_t color;
};
#define MAX_POINTS 50
SurveyPoint points[MAX_POINTS];
int pointCount = 0;

// TJpg_Decoder callback used only for the boot logo: pushes decoded blocks
// straight to the TFT (no PSRAM buffering — unlike the floor-plan decoder
// below, this image is shown once and never panned/zoomed/redrawn).
bool bootLogoOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  tft.drawRGBBitmap(x, y, bitmap, w, h);
  return 1;
}

// TJpg_Decoder callback: writes decoded blocks into the PSRAM buffer
bool bufferOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  for (int row = 0; row < h; row++) {
    int destY = y + row;
    if (destY < 0 || destY >= imgH) continue;
    for (int col = 0; col < w; col++) {
      int destX = x + col;
      if (destX < 0 || destX >= imgW) continue;
      imgBuffer[destY * imgW + destX] = bitmap[row * w + col];
    }
  }
  return 1;
}

// Touches near the edges/corners land consistently pulled in toward the
// center — the calibrated min/max raw values sit slightly inside the
// panel's true physical extremes. TOUCH_MARGIN extrapolates the mapping a
// bit beyond that calibrated range so an edge/corner tap reaches the true
// screen boundary instead of stopping short. If corners are still off,
// increase this; if it now overshoots past the edges, decrease it.
const int TOUCH_MARGIN = 15;   // pixels

// Legitimate raw touch readings top out around X_raw_max/Y_raw_max (~3300).
// A fault reading (IRQ noise with no real contact, or a bad SPI response)
// can saturate EITHER axis independently — (0,0), (8191,8191), and partial
// cases like (8183,0) have all been seen. So this checks both axes
// separately rather than requiring both to be invalid together.
const int16_t RAW_INVALID_THRESHOLD = 4000;
bool isInvalidTouch(TS_Point &p) {
  return (p.x == 0 && p.y == 0) || p.x >= RAW_INVALID_THRESHOLD || p.y >= RAW_INVALID_THRESHOLD;
}

void mapTouch(TS_Point p, int16_t &outX, int16_t &outY) {
  int16_t rawX = p.y;
  int16_t rawY = p.x;
  int16_t x = map(rawY, X_raw_min, X_raw_max, -TOUCH_MARGIN, SCREEN_W - 1 + TOUCH_MARGIN);
  int16_t y = map(rawX, Y_raw_min, Y_raw_max, -TOUCH_MARGIN, SCREEN_H - 1 + TOUCH_MARGIN);
  outX = constrain(x, 0, SCREEN_W - 1);
  outY = constrain(y, 0, SCREEN_H - 1);
}

bool pointInRect(int16_t px, int16_t py, int rx, int ry, int rw, int rh) {
  return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

// Scans the SD card root folder for .jpg/.jpeg files and fills
// floorplanNames[]/floorplanCount. Call after SD.begin().
void scanFloorPlans() {
  floorplanCount = 0;
  File root = SD.open("/");
  if (!root) {
    Serial.println("Could not open SD root to list floor plans");
    return;
  }

  File entry = root.openNextFile();
  while (entry && floorplanCount < MAX_FLOORPLANS) {
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      String lower = name;
      lower.toLowerCase();
      if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
        if (!name.startsWith("/")) name = "/" + name;
        name.toCharArray(floorplanNames[floorplanCount], sizeof(floorplanNames[floorplanCount]));
        floorplanCount++;
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();

  Serial.print("Found ");
  Serial.print(floorplanCount);
  Serial.println(" floor plan(s) on SD card:");
  for (int i = 0; i < floorplanCount; i++) {
    Serial.print("  ");
    Serial.println(floorplanNames[i]);
  }

  // Make each row as tall as possible given how many there are, so a
  // short list (often just one or two floor plans) gets big, easy-to-hit
  // targets instead of a cramped 22px strip. Capped so a long list still
  // fits, floored so it never gets too thin to hit reliably.
  if (floorplanCount > 0) {
    int available = SCREEN_H - LIST_TOP - 4;
    ROW_H = available / floorplanCount;
    if (ROW_H > 44) ROW_H = 44;
    if (ROW_H < 22) ROW_H = 22;
    Serial.print("Picker row height: ");
    Serial.print(ROW_H);
    Serial.println(" px");
  }
}

void drawFloorplanList() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.println("Select a floor plan:");

  if (floorplanCount == 0) {
    tft.setCursor(5, LIST_TOP);
    tft.println("No .jpg files found on SD card");
    return;
  }

  for (int i = 0; i < floorplanCount; i++) {
    int y = LIST_TOP + i * ROW_H;
    tft.fillRect(2, y, SCREEN_W - 4, ROW_H - 2, ILI9341_NAVY);
    tft.drawRect(2, y, SCREEN_W - 4, ROW_H - 2, ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(8, y + (ROW_H - 2) / 2 - 8);   // vertically centered in the row

    // At text size 2, each char is ~12px wide — truncate long filenames
    // so they don't run off the right edge of the row
    char displayName[27];
    strncpy(displayName, floorplanNames[i], sizeof(displayName) - 1);
    displayName[sizeof(displayName) - 1] = '\0';
    tft.println(displayName);
  }
  tft.setTextSize(1);   // restore default used elsewhere
}

// Blocks until the user taps a listed row. Returns the chosen index,
// or -1 if there was nothing to choose from.
// Returns the chosen row index, -1 if there was nothing to choose from,
// or -2 if the green button was pressed to go back to the mode menu
// instead of picking a plan. Waits indefinitely otherwise — no more
// auto-selecting a plan after a timeout, which was a diagnostic aid from
// when touch reliability was still in question and picked a plan the
// user hadn't actually chosen.
int selectFloorplan() {
  drawFloorplanList();
  if (floorplanCount == 0) {
    delay(3000);
    return -1;
  }

  while (true) {
    if (digitalRead(BTN_GREEN) == LOW) {
      waitForRelease(BTN_GREEN);
      return -2;
    }

    if (touch.touched()) {
      TS_Point p = touch.getPoint();

      // Invalid/saturated reading — touch.touched() fired without a real,
      // settled contact (IRQ noise), or SPI didn't get a valid response.
      // Treat it as no touch rather than a failed or accidental tap.
      if (isInvalidTouch(p)) {
        delay(20);
        continue;
      }

      int16_t sx, sy;
      mapTouch(p, sx, sy);

      Serial.print("Picker touch: raw=(");
      Serial.print(p.x);
      Serial.print(",");
      Serial.print(p.y);
      Serial.print(") mapped=(");
      Serial.print(sx);
      Serial.print(",");
      Serial.print(sy);
      Serial.println(")");

      // Hit-test split at the midpoint between rows, with the first row's
      // top and the last row's bottom extended to the true screen edges —
      // same fix as the zoom column: no gap a tap can fall into unclaimed.
      bool hitRow = false;
      for (int i = 0; i < floorplanCount; i++) {
        int hitTop = (i == 0) ? 0 : (LIST_TOP + i * ROW_H - ROW_H / 2);
        int hitBottom = (i == floorplanCount - 1) ? SCREEN_H : (LIST_TOP + (i + 1) * ROW_H - ROW_H / 2);
        if (sy >= hitTop && sy < hitBottom) {
          Serial.print("  -> hit row ");
          Serial.println(i);
          while (touch.touched()) delay(10);   // wait for finger to lift
          return i;
        }
      }
      if (!hitRow) {
        Serial.println("  -> no row hit");
      }
      delay(150);   // debounce a tap that landed between rows
    }
    delay(20);
  }
}

// Builds CSV_FILE from FLOORPLAN_FILE by swapping the extension for .csv,
// e.g. "/first-floor.jpg" -> "/first-floor.csv". Keeps each floor's
// survey log in its own file automatically.
void deriveCsvFilename() {
  strncpy(CSV_FILE, FLOORPLAN_FILE, sizeof(CSV_FILE));
  CSV_FILE[sizeof(CSV_FILE) - 1] = '\0';

  char *dot = strrchr(CSV_FILE, '.');
  if (dot != nullptr) {
    strcpy(dot, ".csv");   // ".csv" is never longer than ".jpg"/".jpeg", so this fits
  } else {
    strncat(CSV_FILE, ".csv", sizeof(CSV_FILE) - strlen(CSV_FILE) - 1);
  }
}

// Build 2: returns the current wall-clock time as "YYYY-MM-DD HH:MM:SS" from
// the RTC, or, if the RTC isn't available this boot, a clearly-marked
// relative fallback ("T+123s") so a reading is never silently mistaken for
// a real timestamp. Used by every CSV logging function in place of the old
// millis()/1000 seconds-since-boot value.
String currentTimestamp() {
  if (app.rtcAvailable) {
    DateTime now = rtc.now();
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    return String(buf);
  }
  return "T+" + String(millis() / 1000) + "s";
}

// Appends one line to CSV_FILE, writing a header first if the file is new.
// Build 7 item 5: `sequence` (a counter shared across every CSV log
// function in the sketch -- see globalLogSequence) is now the first
// column, and `storage` records which filesystem this row actually landed
// on, so gaps/flips are visible later even though Mode 1 itself only ever
// targets SD (it has no FFat fallback -- see pickAndLoadFloorPlan()).
// img_x/img_y (the full-resolution pixel coordinates computed before the
// percentage conversion) are added alongside the existing percentage
// columns, which are kept as-is for anything already parsing them.
void appendCsvLine(float xPercent, float yPercent, int rssi, float imgX, float imgY) {
  bool fileExists = SD.exists(CSV_FILE);

  File f = SD.open(CSV_FILE, FILE_APPEND);
  if (!f) {
    Serial.print("Could not open CSV for append: ");
    Serial.println(CSV_FILE);
    return;
  }

  if (!fileExists) {
    f.println("sequence,storage,floorplan,x_percent,y_percent,img_x,img_y,timestamp,rssi_dbm");
  }

  globalLogSequence++;
  f.print(globalLogSequence);
  f.print(",SD,");
  f.print(FLOORPLAN_FILE);
  f.print(",");
  f.print(xPercent, 2);
  f.print(",");
  f.print(yPercent, 2);
  f.print(",");
  f.print(imgX, 1);
  f.print(",");
  f.print(imgY, 1);
  f.print(",");
  f.print(currentTimestamp());
  f.print(",");
  if (rssi == RSSI_NOT_FOUND) {
    f.println("NOT_FOUND");
  } else {
    f.println(rssi);
  }
  f.close();

  Serial.print("Logged to ");
  Serial.println(CSV_FILE);
}

// Removes the last data line from CSV_FILE (keeps the header). The file is
// small (at most MAX_POINTS short lines), so it's simplest to read it whole
// into RAM, drop everything after the last remaining newline, and rewrite.
void removeLastCsvLine() {
  File src = SD.open(CSV_FILE, FILE_READ);
  if (!src) {
    Serial.print("Could not open CSV to undo last line: ");
    Serial.println(CSV_FILE);
    return;
  }

  String content = "";
  while (src.available()) {
    content += (char)src.read();
  }
  src.close();

  content.trim();   // drop trailing newline so lastIndexOf finds the right one
  int lastNewline = content.lastIndexOf('\n');
  if (lastNewline < 0) {
    Serial.println("CSV has no data line to remove");
    return;
  }
  content = content.substring(0, lastNewline + 1);   // keep everything up to (and including) that newline

  SD.remove(CSV_FILE);   // FILE_WRITE would append, not overwrite, so remove first
  File dst = SD.open(CSV_FILE, FILE_WRITE);
  if (!dst) {
    Serial.print("Could not recreate CSV after undo: ");
    Serial.println(CSV_FILE);
    return;
  }
  dst.print(content);
  dst.close();

  Serial.println("Removed last line from CSV");
}

// Undoes the most recently logged survey point: removes its dot from the
// screen and its line from the CSV file.
void undoLastPoint() {
  if (pointCount == 0) {
    Serial.println("No points to undo");
    return;
  }

  // If the last point's scan hasn't finished yet, no CSV line was written
  // for it at all — just cancel the pending scan and drop the point,
  // rather than removing the *previous* point's CSV row by mistake.
  bool wasPending = (app.scanPending && pendingPointIdx == pointCount - 1);

  pointCount--;
  Serial.print("Undid last point — ");
  Serial.print(pointCount);
  Serial.println(" point(s) remaining on screen");

  if (wasPending) {
    app.scanPending = false;
    pendingPointIdx = -1;
    Serial.println("(cancelled its in-progress scan; no CSV line was written for it)");
  } else {
    removeLastCsvLine();
  }

  needsRedraw = true;   // full renderView() repaint naturally erases the old dot
}

void clampCenter() {
  float visibleW = SCREEN_W / app.zoom;
  float visibleH = SCREEN_H / app.zoom;

  if (visibleW >= imgW) {
    app.centerX = imgW / 2.0;
  } else {
    float minCenterX = visibleW / 2.0;
    float maxCenterX = imgW - visibleW / 2.0;
    if (app.centerX < minCenterX) app.centerX = minCenterX;
    if (app.centerX > maxCenterX) app.centerX = maxCenterX;
  }

  if (visibleH >= imgH) {
    app.centerY = imgH / 2.0;
  } else {
    float minCenterY = visibleH / 2.0;
    float maxCenterY = imgH - visibleH / 2.0;
    if (app.centerY < minCenterY) app.centerY = minCenterY;
    if (app.centerY > maxCenterY) app.centerY = maxCenterY;
  }
}

// Shared by renderView(), drawSurveyPoints() and logSurveyPoint() so the
// screen<->image mapping is always computed identically in one place.
void computeViewOrigin(float &srcX0, float &srcY0) {
  float visibleW = SCREEN_W / app.zoom;
  float visibleH = SCREEN_H / app.zoom;

  srcX0 = app.centerX - visibleW / 2.0;
  srcY0 = app.centerY - visibleH / 2.0;

  if (srcX0 < 0) srcX0 = 0;
  if (srcY0 < 0) srcY0 = 0;
  if (srcX0 + visibleW > imgW) srcX0 = imgW - visibleW;
  if (srcY0 + visibleH > imgH) srcY0 = imgH - visibleH;
  if (srcX0 < 0) srcX0 = 0;
  if (srcY0 < 0) srcY0 = 0;
}

void renderView() {
  unsigned long startTime = millis();

  float srcX0, srcY0;
  computeViewOrigin(srcX0, srcY0);

  static uint16_t lineBuf[SCREEN_W];

  tft.startWrite();
  tft.setAddrWindow(0, 0, SCREEN_W, SCREEN_H);
  for (int screenY = 0; screenY < SCREEN_H; screenY++) {
    int srcY = (int)(srcY0 + screenY / app.zoom);
    if (srcY < 0) srcY = 0;
    if (srcY >= imgH) srcY = imgH - 1;

    for (int screenX = 0; screenX < SCREEN_W; screenX++) {
      int srcXpx = (int)(srcX0 + screenX / app.zoom);
      if (srcXpx < 0) srcXpx = 0;
      if (srcXpx >= imgW) srcXpx = imgW - 1;
      lineBuf[screenX] = imgBuffer[srcY * imgW + srcXpx];
    }
    tft.writePixels(lineBuf, SCREEN_W);
  }
  tft.endWrite();

  unsigned long elapsed = millis() - startTime;
  Serial.print("Render: zoom=");
  Serial.print(app.zoom, 2);
  Serial.print(" center=(");
  Serial.print(app.centerX, 0);
  Serial.print(",");
  Serial.print(app.centerY, 0);
  Serial.print(") took ");
  Serial.print(elapsed);
  Serial.println(" ms");
}

void drawSurveyPoints() {
  float srcX0, srcY0;
  computeViewOrigin(srcX0, srcY0);

  for (int i = 0; i < pointCount; i++) {
    float sx = (points[i].imgX - srcX0) * app.zoom;
    float sy = (points[i].imgY - srcY0) * app.zoom;
    if (sx < -6 || sx > SCREEN_W + 6 || sy < -6 || sy > SCREEN_H + 6) continue;
    tft.fillCircle((int)sx, (int)sy, 4, points[i].color);
    tft.drawCircle((int)sx, (int)sy, 4, ILI9341_BLACK);
  }
}

// Draws the R / - / + column down the right edge. Must be called after
// renderView(), since renderView() overwrites the whole screen.
void drawZoomControls() {
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_WHITE);

  // R — top third
  tft.fillRect(TOOLBAR_X, 0, TOOLBAR_W, BAND_H - 3, ILI9341_NAVY);
  tft.drawRect(TOOLBAR_X, 0, TOOLBAR_W, BAND_H - 3, ILI9341_WHITE);
  tft.setCursor(TOOLBAR_X + 16, BAND_H / 2 - 12);
  tft.print("R");

  // - — middle third
  tft.fillRect(TOOLBAR_X, BAND_H, TOOLBAR_W, BAND_H - 3, ILI9341_NAVY);
  tft.drawRect(TOOLBAR_X, BAND_H, TOOLBAR_W, BAND_H - 3, ILI9341_WHITE);
  tft.setCursor(TOOLBAR_X + 18, BAND_H + BAND_H / 2 - 12);
  tft.print("-");

  // + — bottom third
  tft.fillRect(TOOLBAR_X, 2 * BAND_H, TOOLBAR_W, BAND_H - 3, ILI9341_NAVY);
  tft.drawRect(TOOLBAR_X, 2 * BAND_H, TOOLBAR_W, BAND_H - 3, ILI9341_WHITE);
  tft.setCursor(TOOLBAR_X + 12, 2 * BAND_H + BAND_H / 2 - 12);
  tft.print("+");

  tft.setTextSize(1);   // restore default used elsewhere
}

// Fixed reticle at screen center — the exact spot the red button logs a
// point at. Drawn last, on top of the image/dots/toolbar, every redraw.
// A small gap at the very center keeps the aim point itself unobscured.
void drawCrosshair() {
  const int cx = SCREEN_W / 2;
  const int cy = SCREEN_H / 2;
  const int armLen = 12;
  const int gap = 3;
  uint16_t color = ILI9341_RED;

  tft.drawFastHLine(cx - armLen, cy, armLen - gap, color);
  tft.drawFastHLine(cx + gap, cy, armLen - gap, color);
  tft.drawFastVLine(cx, cy - armLen, armLen - gap, color);
  tft.drawFastVLine(cx, cy + gap, armLen - gap, color);
}

// Pulls the strongest RSSI for activeTargetSsid (Build 8: the network
// picked on-screen when this mode was entered) out of an already-completed
// scan's results (n entries), or RSSI_NOT_FOUND if it wasn't in range.
int bestRssiFromScan(int n) {
  int bestRssi = RSSI_NOT_FOUND;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == activeTargetSsid) {
      int rssi = WiFi.RSSI(i);
      if (bestRssi == RSSI_NOT_FOUND || rssi > bestRssi) {
        bestRssi = rssi;
      }
    }
  }
  return bestRssi;
}

// Called once per loop() iteration. No-op unless a scan is in flight.
void checkPendingScan() {
  if (!app.scanPending) return;

  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;   // still going — check again next loop

  if (n == WIFI_SCAN_FAILED) {
    Serial.println("Scan failed");
    n = 0;
  } else {
    Serial.print("Scan complete (");
    Serial.print(millis() - scanStartMs);
    Serial.print(" ms), ");
    Serial.print(n);
    Serial.println(" networks found");
  }

  int rssi = bestRssiFromScan(n);
  WiFi.scanDelete();

  uint16_t dotColor;
  const char* label;
  if (rssi == RSSI_NOT_FOUND) {
    dotColor = ILI9341_DARKGREY;
    label = "NOT FOUND";
  } else if (rssi >= RSSI_GREEN_THRESHOLD) {
    dotColor = ILI9341_GREEN;
    label = "GOOD";
  } else if (rssi >= RSSI_AMBER_THRESHOLD) {
    dotColor = ILI9341_ORANGE;
    label = "FAIR";
  } else {
    dotColor = ILI9341_RED;
    label = "POOR";
  }

  points[pendingPointIdx].rssi = rssi;
  points[pendingPointIdx].color = dotColor;
  drawSurveyPoints();   // redraw this point in its real color

  Serial.print("SSID '");
  Serial.print(activeTargetSsid);
  Serial.print("' RSSI: ");
  if (rssi == RSSI_NOT_FOUND) {
    Serial.println("not found");
  } else {
    Serial.print(rssi);
    Serial.print(" dBm (");
    Serial.print(label);
    Serial.println(")");
  }
  Serial.print("Point ");
  Serial.print(pointCount);
  Serial.print("/");
  Serial.println(MAX_POINTS);

  appendCsvLine(pendingXPercent, pendingYPercent, rssi, pendingImgX, pendingImgY);

  app.scanPending = false;
  pendingPointIdx = -1;
}

void logSurveyPoint(int16_t screenX, int16_t screenY) {
  if (pointCount >= MAX_POINTS) {
    Serial.println("Survey point storage full, ignoring tap");
    return;
  }

  // A scan from an earlier tap is still running — starting another one
  // now would restart WiFi.scanNetworks() on top of it, silently
  // abandoning the first tap's point (never resolved, never written to
  // CSV) and hammering the WiFi driver with overlapping scan requests,
  // which is likely enough to wedge it. Ignore this tap; the previous
  // one finishes in a second or two and the next tap will go through.
  if (app.scanPending) {
    Serial.println("Scan already in progress — point not logged, try again shortly");
    return;
  }

  float srcX0, srcY0;
  computeViewOrigin(srcX0, srcY0);

  float imgX = srcX0 + screenX / app.zoom;
  float imgY = srcY0 + screenY / app.zoom;
  imgX = constrain(imgX, 0, (float)(imgW - 1));
  imgY = constrain(imgY, 0, (float)(imgH - 1));

  float xPercent = (imgX / (float)imgW) * 100.0;
  float yPercent = (imgY / (float)imgH) * 100.0;

  Serial.print("Tap at screen (");
  Serial.print(screenX);
  Serial.print(",");
  Serial.print(screenY);
  Serial.print(") -> image (");
  Serial.print(imgX, 1);
  Serial.print(",");
  Serial.print(imgY, 1);
  Serial.print(") = (");
  Serial.print(xPercent, 1);
  Serial.print("%, ");
  Serial.print(yPercent, 1);
  Serial.println("%)");

  // Show a pending dot immediately, before the (slow) scan runs, so the
  // tap feels instant. This point gets updated in place once we know
  // the real RSSI, a few lines down.
  int idx = pointCount;
  points[idx].imgX = imgX;
  points[idx].imgY = imgY;
  points[idx].rssi = RSSI_PENDING;
  points[idx].color = ILI9341_WHITE;
  pointCount++;
  drawSurveyPoints();

  // Kick off the scan in the background (non-blocking) and remember which
  // point + CSV row it belongs to; checkPendingScan() finishes the job
  // once WiFi.scanComplete() reports it's done. Pan/zoom/buttons keep
  // working in the meantime instead of freezing for the ~2-3s scan.
  Serial.println("Scanning...");
  scanStartMs = millis();
  WiFi.scanNetworks(true);   // true = async
  app.scanPending = true;
  pendingPointIdx = idx;
  pendingXPercent = xPercent;
  pendingYPercent = yPercent;
  pendingImgX = imgX;
  pendingImgY = imgY;
}

// --- Build 8: on-screen target-network picker (Modes 1/2) ---
// Scans for nearby networks and builds a deduped, strongest-signal-first
// list for selectTargetNetwork() to show. Kicks the scan off async and
// polls scanComplete() rather than calling WiFi.scanNetworks() directly --
// that blocks for ~2-3s, which has bitten this project before (see the
// "async scan state" comment near app.scanPending); worth avoiding here
// too even though this screen has nothing else that needs to keep running
// underneath it.
void scanNetworksForPicker() {
  surveyNetworkCount = 0;

  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 100);
  tft.println("Scanning for networks...");

  WiFi.scanNetworks(true);
  int n;
  while ((n = WiFi.scanComplete()) == WIFI_SCAN_RUNNING) {
    delay(50);
  }

  if (n == WIFI_SCAN_FAILED) {
    Serial.println("Network picker: scan failed");
    WiFi.scanDelete();
    return;
  }

  Serial.print("Network picker: ");
  Serial.print(n);
  Serial.println(" network(s) seen");

  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;   // hidden SSID -- nothing to tap/match later, so leave it out rather than show an unselectable row
    int rssi = WiFi.RSSI(i);

    // Same SSID already in the list (another AP/channel broadcasting it) --
    // keep only the strongest reading, same spirit as the sibling-radio
    // grouping added for Mode 3 in Build 7.6, just simpler (exact-SSID
    // only; this list is for picking a name to type into a scan filter,
    // not for congestion counting).
    int existing = -1;
    for (int k = 0; k < surveyNetworkCount; k++) {
      if (ssid == surveyNetworkNames[k]) { existing = k; break; }
    }
    if (existing >= 0) {
      if (rssi > surveyNetworkRssi[existing]) surveyNetworkRssi[existing] = rssi;
      continue;
    }

    if (surveyNetworkCount < MAX_SURVEY_NETWORKS) {
      ssid.toCharArray(surveyNetworkNames[surveyNetworkCount], sizeof(surveyNetworkNames[surveyNetworkCount]));
      surveyNetworkRssi[surveyNetworkCount] = rssi;
      surveyNetworkCount++;
    } else {
      // List's full -- same bounded "keep the strongest, drop the
      // weakest" trick as buildNearbyApSummary() rather than just
      // ignoring every network seen after the first MAX_SURVEY_NETWORKS.
      int weakestPos = 0;
      for (int k = 1; k < surveyNetworkCount; k++) {
        if (surveyNetworkRssi[k] < surveyNetworkRssi[weakestPos]) weakestPos = k;
      }
      if (rssi > surveyNetworkRssi[weakestPos]) {
        ssid.toCharArray(surveyNetworkNames[weakestPos], sizeof(surveyNetworkNames[weakestPos]));
        surveyNetworkRssi[weakestPos] = rssi;
      }
    }
  }
  WiFi.scanDelete();

  // Strongest signal first -- small insertion sort, surveyNetworkCount is
  // at most MAX_SURVEY_NETWORKS.
  for (int a = 1; a < surveyNetworkCount; a++) {
    char tName[33];
    strncpy(tName, surveyNetworkNames[a], sizeof(tName));
    int tRssi = surveyNetworkRssi[a];
    int b = a - 1;
    while (b >= 0 && surveyNetworkRssi[b] < tRssi) {
      strncpy(surveyNetworkNames[b + 1], surveyNetworkNames[b], sizeof(surveyNetworkNames[b + 1]));
      surveyNetworkRssi[b + 1] = surveyNetworkRssi[b];
      b--;
    }
    strncpy(surveyNetworkNames[b + 1], tName, sizeof(surveyNetworkNames[b + 1]));
    surveyNetworkRssi[b + 1] = tRssi;
  }

  if (surveyNetworkCount == 0) {
    Serial.println("Network picker: no (non-hidden) networks found");
  }
}

void drawNetworkList() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.println("Select network to survey:");

  if (surveyNetworkCount == 0) {
    tft.setCursor(5, NET_LIST_TOP);
    tft.println("No networks found nearby");
    return;
  }

  int available = SCREEN_H - NET_LIST_TOP - 4;
  netRowH = available / surveyNetworkCount;
  if (netRowH > 44) netRowH = 44;
  if (netRowH < 22) netRowH = 22;

  for (int i = 0; i < surveyNetworkCount; i++) {
    int y = NET_LIST_TOP + i * netRowH;
    tft.fillRect(2, y, SCREEN_W - 4, netRowH - 2, ILI9341_NAVY);
    tft.drawRect(2, y, SCREEN_W - 4, netRowH - 2, ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(8, y + (netRowH - 2) / 2 - 8);

    // Leave room on the right for the " -NNdBm" suffix -- truncate long
    // SSIDs rather than let them run into it or off the screen edge.
    char displayName[19];
    strncpy(displayName, surveyNetworkNames[i], sizeof(displayName) - 1);
    displayName[sizeof(displayName) - 1] = '\0';
    tft.print(displayName);
    tft.print(" ");
    tft.print(surveyNetworkRssi[i]);
    tft.println("dBm");
  }
  tft.setTextSize(1);   // restore default used elsewhere
}

// Blocks until the user taps a listed network -- same convention as
// selectFloorplan(): returns the chosen index, -1 if there was nothing to
// choose from, or -2 if Green was pressed to go back to the mode menu
// instead of picking one.
int selectTargetNetwork() {
  scanNetworksForPicker();
  drawNetworkList();
  if (surveyNetworkCount == 0) {
    delay(3000);
    return -1;
  }

  while (true) {
    if (digitalRead(BTN_GREEN) == LOW) {
      waitForRelease(BTN_GREEN);
      return -2;
    }

    if (touch.touched()) {
      TS_Point p = touch.getPoint();
      if (isInvalidTouch(p)) {
        delay(20);
        continue;
      }
      int16_t sx, sy;
      mapTouch(p, sx, sy);

      // Same hit-test convention as selectFloorplan(): split at the
      // midpoint between rows, first row's top and last row's bottom
      // extended to the true screen edges.
      for (int i = 0; i < surveyNetworkCount; i++) {
        int hitTop = (i == 0) ? 0 : (NET_LIST_TOP + i * netRowH - netRowH / 2);
        int hitBottom = (i == surveyNetworkCount - 1) ? SCREEN_H : (NET_LIST_TOP + (i + 1) * netRowH - netRowH / 2);
        if (sy >= hitTop && sy < hitBottom) {
          while (touch.touched()) delay(10);   // wait for finger to lift
          Serial.print("Target network selected: ");
          Serial.println(surveyNetworkNames[i]);
          return i;
        }
      }
      delay(150);   // debounce a tap that landed between rows
    }
    delay(20);
  }
}

// Shows the picker, loads whichever floor plan is chosen, and sets up the
// view/PSRAM buffer for it. Called once from setup(), and again any time
// the green button is pressed to switch to a different floor plan without
// rebooting. Frees any previously-allocated PSRAM buffer first.
void pickAndLoadFloorPlan() {
  // Mode 1 genuinely needs SD (the floor plan JPEGs live there) — unlike
  // Modes 2/3, there's no fallback that makes sense here. Give it one more
  // chance first in case a card's been inserted since boot (or since the
  // last time this failed), rather than permanently writing SD off for
  // the rest of this power-on session.
  if (!app.sdCardAvailable) {
    app.sdCardAvailable = trySDInit();
  }
  if (!app.sdCardAvailable) {
    Serial.println("Mode 1 unavailable: no SD card detected");
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, 5);
    tft.println("No SD card detected.");
    tft.setCursor(5, 20);
    tft.println("Mode 1 needs an SD card for floor plans.");
    tft.setCursor(5, 35);
    tft.println("Insert one and restart, or pick");
    tft.setCursor(5, 50);
    tft.println("Mode 2 or 3 instead.");
    delay(3000);
    enterMode(selectMode());
    return;
  }

  // Free the old image buffer (if any) before allocating a new one —
  // otherwise switching floor plans repeatedly would leak PSRAM.
  if (imgBuffer != nullptr) {
    free(imgBuffer);
    imgBuffer = nullptr;
  }
  pointCount = 0;   // old plan's on-screen dots don't belong on a new plan

  // A scan from the old floor plan is now meaningless — it would otherwise
  // land on the new plan's points[] slot and CSV file once it completes.
  if (app.scanPending) {
    app.scanPending = false;
    pendingPointIdx = -1;
    WiFi.scanDelete();
  }

  scanFloorPlans();
  int chosen = selectFloorplan();

  if (chosen == -2) {
    // Green pressed at the picker — back to the mode menu, not just a
    // re-shown picker.
    Serial.println("Returning to mode menu...");
    enterMode(selectMode());
    return;
  }

  if (chosen < 0) {
    Serial.println("No floor plan selected — nothing to load");
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(5, 5);
    tft.println("No floor plan available");
    return;
  }
  strncpy(FLOORPLAN_FILE, floorplanNames[chosen], sizeof(FLOORPLAN_FILE));
  FLOORPLAN_FILE[sizeof(FLOORPLAN_FILE) - 1] = '\0';
  deriveCsvFilename();
  Serial.print("Loading floor plan: ");
  Serial.println(FLOORPLAN_FILE);
  Serial.print("Logging survey points to: ");
  Serial.println(CSV_FILE);

  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.println("Loading floor plan...");

  uint16_t jpgW = 0, jpgH = 0;
  JRESULT r = TJpgDec.getFsJpgSize(&jpgW, &jpgH, FLOORPLAN_FILE, SD);
  if (r != JDR_OK) {
    Serial.print("Could not read JPEG header, code: ");
    Serial.println(r);
    tft.println("JPEG header read failed");
    return;
  }
  Serial.print("JPEG native size: ");
  Serial.print(jpgW);
  Serial.print(" x ");
  Serial.println(jpgH);

  imgW = jpgW;
  imgH = jpgH;

  size_t bufferBytes = (size_t)imgW * imgH * sizeof(uint16_t);
  Serial.print("Allocating PSRAM buffer: ");
  Serial.print(bufferBytes / 1024);
  Serial.println(" KB");

  imgBuffer = (uint16_t *)ps_malloc(bufferBytes);
  if (imgBuffer == nullptr) {
    Serial.println("PSRAM allocation FAILED - image too large for available PSRAM");
    tft.println("PSRAM alloc failed");
    return;
  }
  Serial.println("PSRAM buffer allocated OK");

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(bufferOutput);

  unsigned long startTime = millis();
  JRESULT decodeResult = TJpgDec.drawSdJpg(0, 0, FLOORPLAN_FILE);
  unsigned long elapsed = millis() - startTime;

  if (decodeResult != JDR_OK) {
    Serial.print("Decode FAILED, code: ");
    Serial.println(decodeResult);
    tft.print("Decode failed, code ");
    tft.println(decodeResult);
    free(imgBuffer);
    imgBuffer = nullptr;
    return;
  }
  Serial.print("Full-res decode into PSRAM took ");
  Serial.print(elapsed);
  Serial.println(" ms");

  float zoomToFitW = (float)SCREEN_W / imgW;
  float zoomToFitH = (float)SCREEN_H / imgH;
  minZoom = min(zoomToFitW, zoomToFitH);
  app.zoom = minZoom;
  app.centerX = imgW / 2.0;
  app.centerY = imgH / 2.0;

  Serial.print("Fit-to-screen zoom level: ");
  Serial.println(minZoom, 3);
  Serial.println("Ready. Drag to pan; red button logs a point at the crosshair.");
  Serial.println("Right-edge column: R=reset, -=zoom out, +=zoom in.");
  Serial.println("Yellow=undo, Blue=clear all, Green=back to floor plan list.");

  needsRedraw = true;
}

// Simple splash screen shown once at power-up, before "Starting up..." and
// the SD retry loop. Shows the Wi-FEye logo, decoded straight from a byte
// array baked into flash (wifeye_logo_data.h) rather than read from the SD
// card — this project's SD/wiring has a history of flaky init, and the
// boot logo should always appear even on a boot where the SD card fails.
// The image is pre-sized to exactly 320x240 (this display's resolution at
// rotation 3) so it's decoded at scale 1 and simply fills the screen.
void showBootScreen() {
  tft.fillScreen(ILI9341_WHITE);

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(bootLogoOutput);
  JRESULT decodeResult = TJpgDec.drawJpg(0, 0, wifeyeLogoJpg, wifeyeLogoJpgLen);
  if (decodeResult != JDR_OK) {
    // Extremely unlikely (the array is fixed at compile time and known-good),
    // but fall back to text rather than leaving a blank screen.
    Serial.print("Boot logo decode failed, code: ");
    Serial.println(decodeResult);
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(3);
    tft.setCursor(40, 110);
    tft.println("Wi-FEye");
  }

  delay(3000);
}

// Build 8.1: pulled out of printResetReason() so logBootEvent() below can
// reuse the exact same wording in the SD/FFat log, rather than keeping a
// second copy of this switch in sync by hand.
const char* resetReasonLabel(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "power-on (fresh power-up)";
    case ESP_RST_EXT:       return "external reset pin";
    case ESP_RST_SW:        return "software reset (e.g. ESP.restart())";
    case ESP_RST_PANIC:     return "PANIC -- unhandled exception/abort in firmware";
    case ESP_RST_INT_WDT:   return "interrupt watchdog timeout -- an ISR or interrupt-disabled section ran too long";
    case ESP_RST_TASK_WDT:  return "task watchdog timeout -- a task hung without yielding";
    case ESP_RST_WDT:       return "other watchdog timeout";
    case ESP_RST_DEEPSLEEP: return "woke from deep sleep";
    case ESP_RST_BROWNOUT:  return "BROWNOUT -- supply voltage dropped too low (battery/current-spike related, not firmware)";
    case ESP_RST_SDIO:      return "SDIO reset";
    default:                return "unknown";
  }
}

// Prints why the board restarted -- a brownout/power-glitch reset, an
// unhandled panic/abort, a task-watchdog timeout (something hung too long
// without yielding), or just a normal power-on/button/firmware-flash reset.
// Added after a field crash during Mode 1 (red-button tap) that didn't
// reproduce over a stable USB connection, to tell a genuine power-supply
// brownout apart from a software fault next time, rather than guessing from
// "it rebooted" alone. esp_reset_reason() is cheap and always available, so
// this stays in permanently rather than only during active debugging.
//
// Build 8.1: this alone turned out not to be enough -- a crash on battery,
// away from a USB connection, has no Serial Monitor listening, so this
// printed line is lost the instant it's written, in exactly the situation
// it's meant to catch. See logBootEvent() below, called right after this
// in setup(), which writes the same information to SD/internal flash
// instead, so it's actually retrievable afterward with no serial connection
// needed at all.
void printResetReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  const char* label = resetReasonLabel(reason);
  Serial.print("Reset reason: ");
  Serial.print((int)reason);
  Serial.print(" (");
  Serial.print(label);
  Serial.println(")");
}

// Build 8.1: writes one row to /boot_log.csv every single boot -- not just
// crashes -- recording the same reset reason printResetReason() prints to
// Serial, but persisted to SD/internal flash instead so it's actually
// retrievable afterward with no serial connection needed (see that
// function's comment for why Serial-only logging isn't enough on its own).
// Deliberately its own file rather than living inside any one mode's CSV:
// a crash can happen during Mode 1 or Mode 2 testing just as easily as
// Mode 3, and this has to be written in setup() before any mode is even
// chosen, so it can't depend on one mode's own log file naming/rotation.
// Appends forever (no auto-incrementing filename like the per-session
// mode logs) -- this is a device-wide boot history meant to grow over the
// unit's whole life, not a single survey session's data.
// Same live SD-preferred/internal-flash-fallback choice as Mode 2/3's
// loggers (preferInternalLogging / trySDInit() re-check), so a row lands
// somewhere even with no SD card fitted.
const char* BOOT_LOG_FILE = "/boot_log.csv";
const char* BOOT_LOG_FALLBACK_FILE = "/boot_log_fallback.csv";

void appendBootLogLineToFallback(int reasonCode, const char* label) {
  bool fileExists = FFat.exists(BOOT_LOG_FALLBACK_FILE);
  File f = FFat.open(BOOT_LOG_FALLBACK_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("Boot log: internal-flash fallback also failed to open -- this boot's row is lost");
    return;
  }
  if (!fileExists) {
    f.println("sequence,storage,timestamp,reset_reason_code,reset_reason_label");
  }
  globalLogSequence++;
  f.print(globalLogSequence);
  f.print(",FFAT,");
  f.print(currentTimestamp());
  f.print(",");
  f.print(reasonCode);
  f.print(",");
  f.println(label);
  f.close();
  Serial.println("Boot log: SD write failed -- row saved to internal flash fallback instead (/boot_log_fallback.csv)");
}

void logBootEvent() {
  esp_reset_reason_t reason = esp_reset_reason();
  const char* label = resetReasonLabel(reason);

  if (preferInternalLogging) {
    appendBootLogLineToFallback((int)reason, label);
    return;
  }

  if (!app.sdCardAvailable) {
    app.sdCardAvailable = trySDInit();
  }
  if (!app.sdCardAvailable) {
    appendBootLogLineToFallback((int)reason, label);
    return;
  }

  bool fileExists = SD.exists(BOOT_LOG_FILE);
  File f = SD.open(BOOT_LOG_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("Boot log: could not open /boot_log.csv for append -- card may have been removed");
    app.sdCardAvailable = false;
    appendBootLogLineToFallback((int)reason, label);
    return;
  }
  if (!fileExists) {
    f.println("sequence,storage,timestamp,reset_reason_code,reset_reason_label");
  }
  globalLogSequence++;
  f.print(globalLogSequence);
  f.print(",SD,");
  f.print(currentTimestamp());
  f.print(",");
  f.print((int)reason);
  f.print(",");
  f.println(label);
  f.close();
  Serial.println("Boot log: row written to /boot_log.csv");
}

void setup() {
  // CPU clock down from the 240MHz default — pure power saving, no
  // diagnostic impact: WiFi scan/connect timing is driven by the radio
  // hardware and driver, not the CPU clock, and 160MHz is still plenty
  // for everything this sketch does (TFT/touch/SD, no heavy compute).
  setCpuFrequencyMhz(160);

  Serial.begin(115200);
  delay(1000);
  Serial.print("=== Wi-FEye, firmware "); Serial.print(FIRMWARE_VERSION); Serial.println(" ==="); // Build 8: dropped the hardcoded "Stage 7" label (a leftover from before FIRMWARE_VERSION existed, and stale again after Build 8's version bump) -- FIRMWARE_VERSION alone is the one label that's actually kept up to date, so the banner no longer carries a second, separately-tracked name alongside it
  printResetReason();
  // Build 8.2: no more compiled-in default to print here -- Modes 1/2 pick
  // a network to survey on-screen each time you enter them (see
  // selectTargetNetwork()/enterMode()), and that's now the ONLY way they
  // get one.

  pinMode(BTN_YELLOW, INPUT_PULLUP);
  pinMode(BTN_BLUE,   INPUT_PULLUP);
  pinMode(BTN_GREEN,  INPUT_PULLUP);
  pinMode(BTN_RED,    INPUT_PULLUP);
  pinMode(BTN_HELP,   INPUT_PULLUP);

  pinMode(PIEZO, OUTPUT);
  noTone(PIEZO);

  // Deselect all shared-bus devices before anything else
  pinMode(TFT_CS, OUTPUT);   digitalWrite(TFT_CS, HIGH);
  pinMode(T_CS, OUTPUT);     digitalWrite(T_CS, HIGH);
  pinMode(SD_CS, OUTPUT);    digitalWrite(SD_CS, HIGH);

  SPI.begin(TFT_CLK, TFT_MISO, TFT_MOSI);

  touch.begin();
  touch.setRotation(1);

  tft.begin();
  tft.setRotation(3);

  showBootScreen();

  // Buttons are already pinMode'd INPUT_PULLUP above, so this can safely
  // read them right away. Runs before SD/FFat init below -- it's a pure
  // TFT+button screen, nothing storage-related needed yet.
  chooseStoragePreference();

  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.println("Starting up...");

  // WiFi in station mode, not connected — scanning works without a
  // connection, and Modes 1/2 are deliberately scan-only (see project
  // notes). Mode 3 is the exception (it actually connects); registered
  // unconditionally here since it's cheap and SysProvEvent() itself
  // ignores everything unless app.currentMode == 3.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.onEvent(SysProvEvent);

  // File-access web server -- routes registered once here (just adds
  // entries to a dictionary, touches nothing network-related). begin()
  // itself opens an actual listening socket, which is deferred until
  // Mode 3 has a real connection (see mode3NeedsWebServerStart in
  // SysProvEvent()/mode3Loop()) rather than being called unconditionally
  // here at boot -- suspected of interfering with the SoftAP provisioning
  // startup when Mode 3 has no stored credentials yet.
  fileServer.on("/", handleFileListPage);
  fileServer.on("/download", handleFileDownload);
  fileServer.on("/delete", HTTP_POST, handleFileDelete);   // Build 7.2: lets a log file be cleared from the device without pulling the SD card

  // The SD card sometimes isn't fully powered up / settled yet by the time
  // we get here, especially with the TFT and touch controller also
  // initializing on the same SPI bus just before it. A single failed
  // SD.begin() used to mean a manual reset; instead, retry a few times,
  // calling SD.end() between attempts so each retry starts clean and
  // resends CMD0 (which forces the card back to idle state).
  app.sdCardAvailable = false;
  for (int attempt = 1; attempt <= 5; attempt++) {
    if (trySDInit()) {
      app.sdCardAvailable = true;
      break;
    }
    Serial.printf("SD card init attempt %d failed, retrying...\n", attempt);
    delay(200);
  }

  // No SD card is no longer a reason to stop here (it used to `return`,
  // halting the whole device before it ever reached the mode menu) — Mode
  // 2 doesn't need SD for its live traffic-light/piezo feedback (only for
  // its CSV log), and Mode 3 can now log entirely via the FFat fallback
  // below (see appendMonitorLogLine). Only Mode 1 genuinely needs SD (the
  // floor plan JPEGs live there) and is guarded separately in
  // pickAndLoadFloorPlan(), which checks app.sdCardAvailable before touching
  // SD at all and sends the user back to the mode menu with a clear
  // message instead of failing partway through.
  if (!app.sdCardAvailable) {
    Serial.println("SD card init FAILED after 5 attempts — continuing without it (Mode 1 will be unavailable)");
    tft.println("No SD card detected — Mode 1 unavailable");
  } else {
    Serial.println("SD card init OK");
  }

  // Internal flash filesystem — used only as a fallback if an SD write
  // fails mid-session in Mode 3 (see appendMonitorLogLine). This is the
  // ESP32's own internal flash chip via a separate controller, not the SD
  // card's SPI bus, so mounting it here can't interfere with or delay the
  // SD init/retry loop above, or the floor-plan scan Mode 1 does on entry.
  // Mounts the existing "FATFS" partition the current Partition Scheme
  // already allocates (12.5MB) via FFat rather than LittleFS/SPIFFS — this
  // board's 16MB-flash partition schemes don't offer a SPIFFS option at
  // all, but FFat mounts that same FATFS partition that's already there,
  // so no Partition Scheme change is needed. format-on-fail (the `true`
  // below) is safe since this partition is only ever used by us.
  if (!FFat.begin(true)) {
    Serial.println("FFat mount failed — Mode 3's internal-flash log fallback won't be available this boot");
  } else {
    Serial.println("FFat mounted OK (internal-flash fallback ready)");
  }

  // --- RTC (DS3231) init -- Build 2 ---
  // Independent of the SPI-bus devices above (separate I2C bus), so this
  // can't interfere with or delay the TFT/touch/SD init done so far.
  Wire.begin(RTC_SDA, RTC_SCL);
  if (!rtc.begin()) {
    Serial.println("RTC not found -- Build 2 timestamps will fall back to seconds-since-boot this session");
    app.rtcAvailable = false;
  } else {
    app.rtcAvailable = true;
    if (rtc.lostPower()) {
      // First power-up ever, or the backup coin cell is dead/missing --
      // either way the RTC has no real time to offer yet. Seed it from the
      // compile time so logs aren't wildly wrong until Mode 3 gets a
      // chance to correct it from NTP.
      Serial.println("RTC lost power (dead/missing backup battery, or first boot) -- seeding from compile time, will correct via NTP in Mode 3");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    Serial.println("RTC OK");
  }

  // --- Auto-incrementing log filenames -- Build 2 ---
  // Computed once per boot (not per mode-menu visit), so re-entering Mode
  // 2/3 repeatedly in one session keeps appending to the same file instead
  // of fragmenting one session's data across several files.
  nextAvailableFilename(SD, "/walk_survey", ".csv", WALK_CSV_FILE, sizeof(WALK_CSV_FILE));
  nextAvailableFilename(FFat, "/walk_survey_fallback", ".csv", WALK_CSV_FALLBACK_FILE, sizeof(WALK_CSV_FALLBACK_FILE));
  nextAvailableFilename(SD, "/monitor_log", ".csv", MONITOR_LOG_FILE, sizeof(MONITOR_LOG_FILE));
  nextAvailableFilename(FFat, "/monitor_log_fallback", ".csv", MONITOR_LOG_FALLBACK_FILE, sizeof(MONITOR_LOG_FALLBACK_FILE));
  Serial.print("This session's log files: ");
  Serial.print(WALK_CSV_FILE);
  Serial.print(", ");
  Serial.println(MONITOR_LOG_FILE);

  // Build 8.1: written here, after storage and the RTC are both ready but
  // before any mode is chosen, so it's captured on EVERY boot regardless
  // of which mode was running when a crash happened or which mode runs
  // next -- see logBootEvent()'s own comment for why this exists
  // alongside printResetReason().
  logBootEvent();

  enterMode(selectMode());
}

// --- Mode 1: floor plan survey ---
// As of this stage, logging a point no longer relies on distinguishing a
// quick tap from a drag by touch timing/movement (a recurring source of
// touch-accuracy trouble). Touch is now pan-only (plus the on-screen zoom
// icons); a fixed on-screen crosshair marks the exact spot that will be
// logged, and the physical red button logs a point there. This also
// sidesteps resistive touch's edge/corner inaccuracy entirely for survey
// points, since the crosshair is always dead center.
void mode1Loop() {
  if (imgBuffer == nullptr) return;

  // --- Help button: show/hide the button reference screen ---
  if (digitalRead(BTN_HELP) == LOW) {
    showHelpScreen(1);
    return;   // skip the rest of this iteration; next loop() redraws normally
  }

  checkPendingScan();

  // --- Red button: log a survey point at the crosshair (screen center) ---
  if (digitalRead(BTN_RED) == LOW) {
    logSurveyPoint(SCREEN_W / 2, SCREEN_H / 2);
    delay(200);
  }

  // --- Blue button: clear on-screen points ---
  if (digitalRead(BTN_BLUE) == LOW) {
    pointCount = 0;
    Serial.println("Survey points cleared from screen (CSV file on SD is untouched)");
    needsRedraw = true;
    delay(150);
  }

  // --- Yellow button: undo the last logged point (screen + CSV) ---
  if (digitalRead(BTN_YELLOW) == LOW) {
    undoLastPoint();
    delay(150);
  }

  // --- Green button: back to the floor plan picker, pick a different one ---
  if (digitalRead(BTN_GREEN) == LOW) {
    waitForRelease(BTN_GREEN);
    Serial.println("Returning to floor plan picker...");
    pickAndLoadFloorPlan();
  }

  // --- Touch: on-screen zoom icons, drag to pan (no more tap-to-log) ---
  if (touch.touched()) {
    TS_Point p = touch.getPoint();

    // Invalid/saturated reading — ignore it rather than acting on a bogus position.
    if (isInvalidTouch(p)) {
      // don't update wasTouching/lastTouchScreen here; wait for a real sample
    } else {
    int16_t sx, sy;
    mapTouch(p, sx, sy);

    if (!wasTouching) {
      // New touch — check the zoom toolbar column first. Bands run the
      // full height, extending to the true top/bottom/right screen edges.
      bool inZoomColumn = (sx >= ZOOM_HIT_LEFT);

      if (inZoomColumn) {
        Serial.print("Zoom column touch: mapped=(");
        Serial.print(sx);
        Serial.print(",");
        Serial.print(sy);
        Serial.print(") -> ");
      }

      if (inZoomColumn && sy < BAND_H) {
        Serial.println("R (reset)");
        app.zoom = minZoom;
        app.centerX = imgW / 2.0;
        app.centerY = imgH / 2.0;
        needsRedraw = true;
        touchOnUI = true;
      } else if (inZoomColumn && sy < 2 * BAND_H) {
        Serial.println("- (zoom out)");
        app.zoom = max(app.zoom / 1.5f, minZoom);
        clampCenter();
        needsRedraw = true;
        touchOnUI = true;
      } else if (inZoomColumn) {
        Serial.println("+ (zoom in)");
        app.zoom = min(app.zoom * 1.5f, maxZoom);
        clampCenter();
        needsRedraw = true;
        touchOnUI = true;
      } else {
        touchOnUI = false;
      }
    } else if (!touchOnUI) {
      int16_t dxScreen = sx - lastTouchScreenX;
      int16_t dyScreen = sy - lastTouchScreenY;

      if (dxScreen != 0 || dyScreen != 0) {
        app.centerX -= dxScreen / app.zoom;
        app.centerY -= dyScreen / app.zoom;
        clampCenter();
        needsRedraw = true;
      }
    }
    lastTouchScreenX = sx;
    lastTouchScreenY = sy;
    wasTouching = true;
    }
  } else {
    wasTouching = false;
    touchOnUI = false;
  }

  if (needsRedraw) {
    unsigned long now = millis();
    // While actively dragging, cap the redraw rate so touch keeps getting
    // polled at full speed between repaints; a discrete zoom-icon tap or
    // the end of a drag isn't rate-limited (wasTouching false), so it
    // still redraws immediately and feels responsive.
    if (!wasTouching || (now - lastRenderMs) >= MIN_RENDER_INTERVAL_MS) {
      renderView();
      drawSurveyPoints();
      drawZoomControls();
      drawCrosshair();
      needsRedraw = false;
      lastRenderMs = now;
    }
  }
}

// ===========================================================================
// Help screen — same button, same behavior, in all 3 modes: shows what the
// other buttons currently do, waits for a fresh press-and-release of
// BTN_HELP to dismiss, then restores whatever was on screen before. This
// blocks the calling mode's loop() for as long as the help screen is up,
// which is the point — "pauses any activity" while it's showing. Modes 1/2
// only ever scan (no live connection to babysit), and Mode 3's actual WiFi
// connection/event handling runs on its own FreeRTOS task independent of
// this loop, so a drop is still noticed and still gets logged the moment
// mode3Loop() resumes — nothing is missed, just briefly delayed.
// ===========================================================================
void showHelpScreen(int mode) {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.println("Help");

  // Body text bumped from size 1 to size 2 (queued from last build, now
  // done). At size 2 each line only has room for ~25 characters before
  // running off the right edge (Adafruit_GFX text doesn't wrap), so every
  // line below is deliberately shortened/reworded to fit rather than just
  // doubling the old wording. lineH is 18, not the proportional 24, to
  // keep Mode 3's longer content (now including Build 1's running
  // summary) from running into the footer.
  int y = 32;
  const int lineH = 18;
  if (mode == 1) {
    tft.setCursor(10, y); tft.println("MODE 1: FLOOR PLAN"); y += lineH * 2;
    tft.setCursor(10, y); tft.println("Red: log point"); y += lineH;
    tft.setCursor(10, y); tft.println("Yellow: undo point"); y += lineH;
    tft.setCursor(10, y); tft.println("Blue: clear screen"); y += lineH;
    tft.setCursor(10, y); tft.println("(CSV kept)"); y += lineH;
    tft.setCursor(10, y); tft.println("Green: back to list"); y += lineH;
    tft.setCursor(10, y); tft.println("(press again: menu)"); y += lineH;
    tft.setCursor(10, y); tft.println("Drag: pan plan"); y += lineH;
    tft.setCursor(10, y); tft.println("Edge: R/-/+ zoom");
  } else if (mode == 2) {
    tft.setCursor(10, y); tft.println("MODE 2: WALK SURVEY"); y += lineH * 2;
    tft.setCursor(10, y); tft.println("Yellow: mute piezo"); y += lineH;
    tft.setCursor(10, y); tft.println("Blue: log zone chg"); y += lineH;
    tft.setCursor(10, y); tft.println("Green: back to menu"); y += lineH;
    tft.setCursor(10, y); tft.println("Light: grn/amb/red"); y += lineH;
    tft.setCursor(10, y); tft.println("Piezo: faster=worse"); y += lineH;
    tft.setCursor(10, y); tft.println("(silent when green)");
  } else {
    tft.setCursor(10, y); tft.println("MODE 3: MONITOR"); y += lineH * 2;
    tft.setCursor(10, y); tft.println("Blue 2s: forget net"); y += lineH;
    tft.setCursor(10, y); tft.println("Yellow: file address"); y += lineH;
    tft.setCursor(10, y); tft.println("Red: toggle alert"); y += lineH;
    tft.setCursor(10, y); tft.println("Green: back to menu"); y += lineH;

    // --- Running summary for this monitoring session (Build 1) ---
    unsigned long now = millis();
    unsigned long elapsedTotal = now - mode3MonitorStartMs;
    unsigned long connectedSoFar = mode3ConnectedAccumMs;
    if (app.mode3State == MODE3_CONNECTED) connectedSoFar += now - mode3LastStateChangeMs;
    float uptimePct = (elapsedTotal > 0) ? (100.0f * connectedSoFar / elapsedTotal) : 0.0f;

    tft.setTextColor(ILI9341_CYAN);
    tft.setCursor(10, y); tft.println("Session summary:"); y += lineH;
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(10, y);
    tft.print("Uptime: "); tft.print(uptimePct, 1); tft.println("%"); y += lineH;
    tft.setCursor(10, y);
    tft.print("Drops: "); tft.println(mode3DropCount); y += lineH;
    tft.setCursor(10, y);
    tft.print("Avg RSSI: ");
    if (mode3RssiSampleCount > 0) {
      tft.print((int)(mode3RssiSum / mode3RssiSampleCount));
      tft.println(" dBm");
    } else {
      tft.println("--");
    }
  }

  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(10, SCREEN_H - 15);
  tft.println("Press Help to return.");

  // Build 7 item 2: Mode 3's Help screen is now non-blocking -- mark the
  // overlay active and return immediately so mode3Loop() keeps servicing
  // WiFi events, logging and the web server underneath it. Dismissal
  // (another Help press, or the same 5-minute timeout as before) is
  // polled from mode3Loop() itself rather than waited for here.
  if (mode == 3) {
    waitForRelease(BTN_HELP);   // the press that opened this screen is still being held
    app.mode3HelpOverlayActive = true;
    app.mode3HelpOverlayShownMs = millis();
    return;
  }

  // Modes 1/2: unchanged, still blocking -- neither has a live connection
  // to babysit while this is up (see the big comment above this function).
  // The press that got us here is still being held when we arrive — wait
  // for it to be released, then block until the *next* press (and its own
  // release) before leaving. Same bounded-wait helper used everywhere else
  // in this sketch, so a stuck/flaky Help pin can't trap the device here
  // forever either.
  waitForRelease(BTN_HELP);
  unsigned long waitStart = millis();
  while (digitalRead(BTN_HELP) == HIGH) {
    if (millis() - waitStart > 5UL * 60UL * 1000UL) {   // 5 minutes — generous, but not forever
      Serial.println("Help screen: no button press seen for 5 minutes, returning automatically.");
      break;
    }
    delay(10);
  }
  waitForRelease(BTN_HELP);

  // Force whatever mode we're returning to redraw its normal screen —
  // each mode has its own idea of "redraw", so restore it the way that
  // mode already does.
  if (mode == 1) {
    needsRedraw = true;
  } else {
    lastDisplayedBand = -1;   // same trick mode2Loop() uses elsewhere to force a redraw
  }
}

// ===========================================================================
// Mode select menu
// ===========================================================================

// Blocks until the user taps one of the 3 mode boxes. Returns 1, 2 or 3.
// ===========================================================================
// "More" menu (Build 7 item 7) -- Diagnostics + File Browser, reached via
// the 4th box on the mode picker. Both sub-screens are simple, blocking,
// button-dismissed text screens: unlike Mode 3's Help/File-access
// overlays, there's no live connection or logging to keep servicing while
// either of these is up, so there's no need for the non-blocking overlay
// machinery item 2 added there.
// ===========================================================================

// Shows free heap/PSRAM, storage/RTC/WiFi availability, firmware version,
// uptime, and (Build 7 items 3/4) the most recent WiFi disconnect reason
// recorded this boot, if any.
void showDiagnosticsScreen() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.println("Diagnostics");

  tft.setTextSize(1);
  int y = 32;
  const int lineH = 15;

  tft.setCursor(10, y); tft.print("Firmware: "); tft.println(FIRMWARE_VERSION); y += lineH;

  unsigned long upSec = millis() / 1000;
  tft.setCursor(10, y);
  tft.print("Uptime: ");
  tft.print(upSec / 3600); tft.print("h ");
  tft.print((upSec % 3600) / 60); tft.print("m ");
  tft.print(upSec % 60); tft.println("s");
  y += lineH;

  tft.setCursor(10, y);
  tft.print("Heap free: "); tft.print(ESP.getFreeHeap() / 1024); tft.println(" KB");
  y += lineH;

  tft.setCursor(10, y);
  tft.print("PSRAM free: ");
  if (ESP.getPsramSize() > 0) {   // guard: PSRAM may not be enabled/present on every build
    tft.print(ESP.getFreePsram() / 1024);
    tft.println(" KB");
  } else {
    tft.println("not available");
  }
  y += lineH;

  tft.setCursor(10, y); tft.print("SD card: "); tft.println(app.sdCardAvailable ? "available" : "not detected"); y += lineH;
  tft.setCursor(10, y); tft.print("RTC: "); tft.println(app.rtcAvailable ? "available" : "not detected"); y += lineH;

  tft.setCursor(10, y);
  tft.print("WiFi: ");
  if (WiFi.status() == WL_CONNECTED) {
    tft.println(WiFi.SSID());
    y += lineH;
    tft.setCursor(10, y);
    tft.print("IP: ");
    tft.println(WiFi.localIP());
  } else {
    tft.println("not connected");
  }
  y += lineH;

  tft.setCursor(10, y);
  tft.print("Last disconnect: ");
  if (app.mode3HaveLastDisconnectReason) {
    tft.println(wifiDisconnectReasonToString(app.mode3LastDisconnectReason));
  } else {
    tft.println("none this boot");
  }
  y += lineH;

  // Build 7.1: the channel-survey result, once the first scan after
  // connecting has completed (mode3RecommendedChannel stays 0 until then).
  tft.setCursor(10, y);
  tft.print("Best ch (1/6/11): ");
  if (mode3RecommendedChannel > 0) {
    tft.print(mode3RecommendedChannel);
    if (mode3CongestionChannel > 0 && mode3RecommendedChannel == mode3CongestionChannel) {
      tft.println(" (current)");
    } else {
      tft.println();
    }
  } else {
    tft.println("not scanned yet");
  }
  y += lineH;

  // A dropped-sample/sequence-gap indicator was optional per the spec --
  // skipped here: detecting one cheaply would mean persisting the last
  // sequence number across reboots (e.g. a tiny marker file on SD/FFat)
  // just to compare against globalLogSequence's fresh-every-boot start at
  // 0, which felt like real added complexity/risk for a "nice to have".
  // Noted explicitly rather than silently left out.

  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(10, SCREEN_H - 15);
  tft.println("Press Green to return.");

  waitForRelease(BTN_GREEN);
  while (digitalRead(BTN_GREEN) == HIGH) delay(10);
  waitForRelease(BTN_GREEN);
}

// Build 7.2: this used to just print instructions pointing at Mode 3's
// web server ("go start Mode 3, connect, then check Yellow for the
// address") -- that's an awkward detour if all you want is to grab or
// clear old logs. This now connects and runs the file-access web server
// itself, the same way Mode 3 does, so the address and the working file
// list are right here. It reuses whatever credentials are already
// stored (the same ones Mode 3's beginProvision() uses) -- it does NOT
// run its own SoftAP/provisioning flow, so if nothing's been provisioned
// yet this will just time out and say so; provisioning is still done via
// Mode 3 (Blue-hold "forget network" + the "ESP SoftAP Prov" app).
void showFileBrowserScreen() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.println("File Browser");
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.println("Connecting to WiFi...");
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(10, SCREEN_H - 15);
  tft.println("Press Green to cancel.");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin();   // no args: reconnects with whatever credentials are already stored in flash

  unsigned long connectStartMs = millis();
  const unsigned long CONNECT_TIMEOUT_MS = 15000;
  bool cancelled = false;
  while (WiFi.status() != WL_CONNECTED && millis() - connectStartMs < CONNECT_TIMEOUT_MS) {
    if (digitalRead(BTN_GREEN) == LOW) { cancelled = true; break; }
    delay(100);
  }

  if (cancelled || WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 5);
    tft.println("File Browser");
    tft.setTextSize(1);
    int y = 40;
    const int lineH = 14;
    if (cancelled) {
      tft.setCursor(10, y); tft.println("Cancelled.");
    } else {
      tft.setTextColor(ILI9341_ORANGE);
      tft.setCursor(10, y); tft.println("Could not connect to WiFi."); y += lineH;
      tft.setCursor(10, y); tft.println("Either nothing's been"); y += lineH;
      tft.setCursor(10, y); tft.println("provisioned yet, or the"); y += lineH;
      tft.setCursor(10, y); tft.println("stored network is out of"); y += lineH;
      tft.setCursor(10, y); tft.println("range."); y += lineH * 2;
      tft.setTextColor(ILI9341_WHITE);
      tft.setCursor(10, y); tft.println("Provision one via Mode 3"); y += lineH;
      tft.setCursor(10, y); tft.println("(Monitor) first.");
    }
    tft.setTextColor(ILI9341_YELLOW);
    tft.setCursor(10, SCREEN_H - 15);
    tft.println("Press Green to return.");
    waitForRelease(BTN_GREEN);
    while (digitalRead(BTN_GREEN) == HIGH) delay(10);
    waitForRelease(BTN_GREEN);
    return;
  }

  // fileServer's routes ("/", "/download", "/delete") were already
  // registered once in setup() -- begin() here just opens the listening
  // socket, same deferred-start approach Mode 3 uses and for the same
  // reason (touching the WiFi/socket layer before a connection exists
  // has caused trouble elsewhere in this project).
  fileServer.begin();
  Serial.println("File Browser: web server started");

  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.println("File Browser");
  tft.setTextSize(1);
  int y = 40;
  const int lineH = 14;
  tft.setCursor(10, y); tft.println("Connected. Open in a browser"); y += lineH;
  tft.setCursor(10, y); tft.println("on the same network:"); y += lineH * 2;
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);
  tft.setCursor(10, y);
  tft.print("http://"); tft.println(WiFi.localIP());
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  y += 36;
  tft.setCursor(10, y); tft.println("Files can be downloaded or"); y += lineH;
  tft.setCursor(10, y); tft.println("deleted from that page.");

  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(10, SCREEN_H - 15);
  tft.println("Press Green to disconnect & return.");

  waitForRelease(BTN_GREEN);   // the press that opened this screen (from showMoreMenu()'s picker) is still being held
  while (digitalRead(BTN_GREEN) == HIGH) {
    fileServer.handleClient();
    delay(5);
  }
  waitForRelease(BTN_GREEN);

  // Same "leave it clean" convention as Mode 3's own Green-button exit --
  // don't stay associated in the background once you've backed out of
  // the screen that was using the connection.
  WiFi.disconnect();
  Serial.println("File Browser: disconnected, returning to menu");
}

// Reached via the mode picker's 4th box. Same blocking tap-driven picker
// style as selectMode()/selectFloorplan(); loops so Green (or picking
// "Back") returns to the main mode picker rather than this submenu
// re-showing itself.
void showMoreMenu() {
  const int boxTop = 50;
  const int boxH = 55;
  const int boxGap = 10;
  const int NUM_BOXES = 3;
  const char* labels[NUM_BOXES] = { "Diagnostics", "File Browser", "Back" };

  while (true) {
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("More:");

    for (int i = 0; i < NUM_BOXES; i++) {
      int y = boxTop + i * (boxH + boxGap);
      tft.fillRect(10, y, SCREEN_W - 20, boxH, ILI9341_NAVY);
      tft.drawRect(10, y, SCREEN_W - 20, boxH, ILI9341_WHITE);
      tft.setTextSize(2);
      tft.setCursor(20, y + boxH / 2 - 8);
      tft.print(labels[i]);
    }
    tft.setTextSize(1);

    int choice = -1;
    while (choice < 0) {
      if (digitalRead(BTN_GREEN) == LOW) {
        waitForRelease(BTN_GREEN);
        choice = NUM_BOXES - 1;   // Green == "Back", same convention as every other screen in this sketch
        break;
      }
      if (touch.touched()) {
        TS_Point p = touch.getPoint();
        if (isInvalidTouch(p)) { delay(20); continue; }
        int16_t sx, sy;
        mapTouch(p, sx, sy);
        for (int i = 0; i < NUM_BOXES; i++) {
          int y = boxTop + i * (boxH + boxGap);
          int yTop = (i == 0) ? 0 : (boxTop + (i - 1) * (boxH + boxGap) + boxH + boxGap / 2);
          int yBot = (i == NUM_BOXES - 1) ? SCREEN_H : (y + boxH + boxGap / 2);
          if (sy >= yTop && sy < yBot) {
            while (touch.touched()) delay(10);
            choice = i;
            break;
          }
        }
      }
      delay(20);
    }

    if (choice == 0) {
      showDiagnosticsScreen();
    } else if (choice == 1) {
      showFileBrowserScreen();
    } else {
      return;   // back to the main mode picker
    }
  }
}

// Build 7 item 7: a 4th box, "More", added alongside the original 3 --
// boxH/boxGap shrunk slightly so all 4 still fill the screen edge-to-edge
// the same way the original 3 did. "More" isn't a real running mode (see
// enterMode()) -- it leads to a Diagnostics/File-Browser submenu and
// always falls back to a fresh selectMode() call afterward.
int selectMode() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.println("Select mode:");

  const int boxTop = 30;
  const int boxH = 48;
  const int boxGap = 6;
  const int NUM_BOXES = 4;
  const char* labels[NUM_BOXES] = {
    "1: Floor plan survey",
    "2: Walk (no plan)",
    "3: Monitor (stationary)",
    "4: More"
  };

  for (int i = 0; i < NUM_BOXES; i++) {
    int y = boxTop + i * (boxH + boxGap);
    tft.fillRect(10, y, SCREEN_W - 20, boxH, ILI9341_NAVY);
    tft.drawRect(10, y, SCREEN_W - 20, boxH, ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, y + boxH / 2 - 8);
    tft.print(labels[i]);
  }
  tft.setTextSize(1);

  while (true) {
    if (touch.touched()) {
      TS_Point p = touch.getPoint();
      if (isInvalidTouch(p)) {
        delay(20);
        continue;
      }
      int16_t sx, sy;
      mapTouch(p, sx, sy);

      for (int i = 0; i < NUM_BOXES; i++) {
        int y = boxTop + i * (boxH + boxGap);
        // Same lesson as the floor plan picker and zoom column: split hit
        // zones at the midpoint between boxes, with the first box's top
        // and the last box's bottom extended to the true screen edges so
        // no tap can land in an unclaimed gap.
        int yTop = (i == 0) ? 0 : (boxTop + (i - 1) * (boxH + boxGap) + boxH + boxGap / 2);
        int yBot = (i == NUM_BOXES - 1) ? SCREEN_H : (y + boxH + boxGap / 2);
        if (sy >= yTop && sy < yBot) {
          while (touch.touched()) delay(10);   // wait for finger to lift
          Serial.print("Mode selected: ");
          Serial.println(i + 1);
          return i + 1;
        }
      }
    }
    delay(20);
  }
}

// Dispatches to whichever mode was chosen — shared by setup() and every
// "back to mode menu" path so they all start that mode the same way.
// Build 8: Modes 1/2 get an extra step first -- picking which network to
// survey (see selectTargetNetwork() above) -- since they no longer only
// ever look for the hardcoded TARGET_SSID. Deliberately placed here
// rather than inside pickAndLoadFloorPlan() itself, since that function is
// also called again by Mode 1's own Green button (switch floor plan,
// same network); re-prompting for a network on every floor-plan switch
// would be a needless extra tap for the common case of staying at one
// site and only wanting a different plan.
void enterMode(int mode) {
  app.currentMode = mode;
  if (mode == 1 || mode == 2) {
    int chosen = selectTargetNetwork();
    if (chosen == -2) {
      // Green pressed at the picker -- back to the mode menu, same
      // convention as the floor-plan and mode-select screens.
      Serial.println("Returning to mode menu...");
      enterMode(selectMode());
      return;
    }
    if (chosen < 0) {
      // No networks found nearby -- nothing to survey either way, so
      // there's no point entering Mode 1/2 at all.
      Serial.println("No networks to survey — returning to mode menu...");
      enterMode(selectMode());
      return;
    }
    activeTargetSsid = String(surveyNetworkNames[chosen]);
    Serial.print("Active target SSID for this session: ");
    Serial.println(activeTargetSsid);
  }

  if (mode == 1) pickAndLoadFloorPlan();
  else if (mode == 2) startMode2();
  else if (mode == 3) startMode3();
  else {
    // "More" (Build 7 item 7) -- not a real running mode, so it never
    // reaches loop()'s dispatcher (which only knows modes 1-3): this call
    // is synchronous/blocking, same as pickAndLoadFloorPlan()'s own
    // "green -> back to menu" recursion elsewhere in this file, and
    // always lands back on a fresh mode pick afterward.
    showMoreMenu();
    enterMode(selectMode());
  }
}

// ===========================================================================
// Mode 2: walk, no floor plan — traffic light + piezo + zone button
// ===========================================================================

void startMode2() {
  app.currentZone = 1;
  app.piezoMuted = false;
  rssiHistoryCount = 0;
  ambientScanPending = false;
  lastDisplayedBand = -1;
  lastDisplayedZone = -1;
  haveAmbientReading = false;
  noTone(PIEZO);

  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.println("Mode 2: walk survey. Starting first scan...");

  // Kick off the first scan right away rather than waiting a full
  // AMBIENT_SCAN_INTERVAL_MS for the first reading to appear.
  WiFi.scanNetworks(true);
  ambientScanPending = true;
  ambientScanStartMs = millis();
  lastAmbientScanStartMs = millis();
}

// Same idea as bestRssiFromScan() (used by Mode 1), but also returns the
// channel the target SSID was found on, needed for the congestion check
// below. Kept separate from Mode 1's version rather than changing its
// signature, so Mode 1's tested behavior is untouched.
int bestRssiAndChannel(int n, int &channelOut) {
  int bestRssi = RSSI_NOT_FOUND;
  channelOut = -1;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == activeTargetSsid) {
      int rssi = WiFi.RSSI(i);
      if (bestRssi == RSSI_NOT_FOUND || rssi > bestRssi) {
        bestRssi = rssi;
        channelOut = WiFi.channel(i);
      }
    }
  }
  return bestRssi;
}

// Counts other (non-target) networks sharing the target's channel with a
// signal strong enough to plausibly be competing for airtime. A rough
// proxy for "is this channel crowded", using only data we already have
// from the same scan — no extra hardware or scanning needed.
int countNearbyApsOnChannel(int n, int targetChannel) {
  if (targetChannel <= 0) return 0;
  int count = 0;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == activeTargetSsid) continue;
    if (WiFi.channel(i) == targetChannel && WiFi.RSSI(i) > -80) count++;
  }
  return count;
}

// Builds the plain-English recommendation line written to SD for one
// reading. Deliberately simple, rule-based advice rather than anything
// clever — the goal is something a non-technical person can act on.
//
// Takes the SMOOTHED reading (notFound flag + avgRssi), not the raw
// single-scan RSSI — this must agree with whatever the traffic light and
// piezo are currently showing, otherwise a stray dropout can turn the
// display red/urgent while the logged recommendation still says "weak"
// based on that one scan's own decent reading (or vice versa).
// Build 7.2: added recommendedChannel (0 = "not available" -- no default
// argument here, since Arduino's auto-prototype generator can mishandle
// default arguments in a plain .ino sketch; every call site passes it
// explicitly instead). Mode 2 never runs the full 1-13 channel survey (it
// only ever scans for the hardcoded TARGET_SSID's own channel), so its
// call site just passes 0 and gets the old generic wording. Mode 3 passes
// mode3RecommendedChannel
// -- the Diagnostics screen already shows that value, but until now the
// logged per-SAMPLE recommendation text never did, even moments after a
// CHANNEL_SURVEY row had just worked out a specific better channel. That
// meant a report built from the log only ever saw "try switching channel"
// repeated, with the actually-useful answer (switch to WHICH channel)
// sitting unread in a separate row type.
String buildRecommendation(bool notFound, int avgRssi, int nearbyApCount, int channel, int recommendedChannel) {
  String rec;
  if (notFound) {
    rec = "Not detected here. Consider an access point or repeater nearby.";
    return rec;
  }

  if (avgRssi >= RSSI_GREEN_THRESHOLD) {
    rec = "Good signal. No changes needed.";
  } else if (avgRssi >= RSSI_AMBER_THRESHOLD) {
    rec = "Usable but weak. Try moving closer to the router or reducing obstacles.";
  } else {
    rec = "Poor signal. Consider a repeater/mesh node here, or relocating the router.";
  }

  if (nearbyApCount >= 2) {
    rec += " Channel " + String(channel) + " looks congested (" + String(nearbyApCount) +
           " other nearby networks on it) — try switching channel";
    if (recommendedChannel > 0 && recommendedChannel != channel) {
      rec += " (channel " + String(recommendedChannel) + " currently recommended).";
    } else {
      rec += ".";
    }
  }

  return rec;
}

// Appends one reading to WALK_CSV_FILE, writing a header first if new.
// Logs both the raw instantaneous reading (rssi, this scan only) and the
// smoothed value the recommendation was actually based on (avgRssi), so
// the two numbers stay distinguishable if you're reviewing the file later.
// Internal-flash fallback for Mode 2's walk-survey log — mirrors
// appendMonitorLogLineToFallback() (Mode 3). SD stays primary; this only
// catches a line that would otherwise be silently dropped, whether SD is
// missing entirely (see app.sdCardAvailable) or just fails a write mid-walk.
// Build 7 item 5: added an `event` column (SAMPLE for every normal reading,
// ZONE_CHANGE for the new explicit zone-change row -- see mode2Loop()),
// plus the shared `sequence` counter and a `storage` column recording
// which filesystem this particular row landed on.
void appendWalkCsvLineToFallback(const char *event, int zone, int rssi, int avgRssi, const String &recommendation) {
  bool fileExists = FFat.exists(WALK_CSV_FALLBACK_FILE);
  File f = FFat.open(WALK_CSV_FALLBACK_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("Mode 2: internal-flash fallback log also failed to open — line lost");
    return;
  }
  if (!fileExists) {
    f.println("sequence,storage,event,zone,timestamp,rssi_dbm,avg_rssi_dbm,recommendation");
  }
  globalLogSequence++;
  f.print(globalLogSequence);
  f.print(",FFAT,");
  f.print(event);
  f.print(",");
  f.print(zone);
  f.print(",");
  f.print(currentTimestamp());
  f.print(",");
  if (rssi == RSSI_NOT_FOUND) {
    f.print("NOT_FOUND");
  } else {
    f.print(rssi);
  }
  f.print(",");
  f.print(avgRssi);
  f.print(",");
  f.println(recommendation);
  f.close();
  Serial.println("Mode 2: SD write failed (or no card) — line saved to internal flash fallback instead (/walk_survey_fallback.csv)");
}

void appendWalkCsvLine(const char *event, int zone, int rssi, int avgRssi, const String &recommendation) {
  // Internal flash preferred (chosen at boot, see chooseStoragePreference())
  // -- always use it directly, never even try SD. No further fallback
  // needed the other way: internal flash has no removable media to fail.
  if (preferInternalLogging) {
    appendWalkCsvLineToFallback(event, zone, rssi, avgRssi, recommendation);
    return;
  }

  // SD preferred (the default) -- live choice, re-checked every line
  // rather than decided once at boot: SD whenever it's actually there,
  // internal flash whenever it isn't -- either way, something gets
  // logged. If SD was marked unavailable (missing at boot, or a previous
  // write here failed), give it a fresh chance in case a card's been
  // inserted since.
  if (!app.sdCardAvailable) {
    app.sdCardAvailable = trySDInit();
  }
  if (!app.sdCardAvailable) {
    appendWalkCsvLineToFallback(event, zone, rssi, avgRssi, recommendation);
    return;
  }

  bool fileExists = SD.exists(WALK_CSV_FILE);
  File f = SD.open(WALK_CSV_FILE, FILE_APPEND);
  if (!f) {
    // The card answered trySDInit() but this specific open/write still
    // failed -- most likely it's just been pulled. Mark it unavailable so
    // the next line (here or in Mode 3) re-checks rather than retrying a
    // dead card every time, and log this one to flash instead.
    Serial.println("Could not open walk survey CSV for append -- card may have been removed");
    app.sdCardAvailable = false;
    appendWalkCsvLineToFallback(event, zone, rssi, avgRssi, recommendation);
    return;
  }
  if (!fileExists) {
    f.println("sequence,storage,event,zone,timestamp,rssi_dbm,avg_rssi_dbm,recommendation");
  }
  globalLogSequence++;
  f.print(globalLogSequence);
  f.print(",SD,");
  f.print(event);
  f.print(",");
  f.print(zone);
  f.print(",");
  f.print(currentTimestamp());
  f.print(",");
  if (rssi == RSSI_NOT_FOUND) {
    f.print("NOT_FOUND");
  } else {
    f.print(rssi);
  }
  f.print(",");
  f.print(avgRssi);
  f.print(",");
  f.println(recommendation);
  f.close();
}

// Redraws the traffic light + zone/RSSI text. Only called when something
// actually changed (band or zone), so it doesn't flicker every loop.
void drawTrafficLight(int band, int displayRssi, bool notFound) {
  uint16_t color;
  const char* label;
  if (notFound) {
    color = ILI9341_DARKGREY;
    label = "NOT FOUND";
  } else if (band == 0) {
    color = ILI9341_GREEN;
    label = "GOOD";
  } else if (band == 1) {
    color = ILI9341_ORANGE;
    label = "FAIR";
  } else {
    color = ILI9341_RED;
    label = "POOR";
  }

  tft.fillScreen(ILI9341_BLACK);
  tft.fillCircle(SCREEN_W / 2, 110, 70, color);

  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(SCREEN_W / 2 - w / 2, 200);
  tft.print(label);

  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.print("Zone: ");
  tft.print(app.currentZone);

  tft.setCursor(SCREEN_W - 90, 5);
  if (notFound) {
    tft.print("RSSI: --");
  } else {
    tft.print("RSSI: ");
    tft.print(displayRssi);
    tft.print(" dBm");
  }

  tft.setCursor(5, 225);
  tft.print(app.piezoMuted ? "Piezo: muted" : "Piezo: on");
}

// Non-blocking beeper: silent when strong, chirping faster as signal gets
// worse. tone(pin, freq, duration) on the ESP32 core auto-stops itself
// after `duration`, so this doesn't need to track on/off state manually.
// avgRssi is only meaningful when !notFound; band/notFound decide which
// regime we're in, avgRssi gives a smooth interval within that regime
// instead of 2-3 abrupt fixed rates.
void updatePiezo(int band, bool notFound, int avgRssi) {
  if (app.piezoMuted) {
    noTone(PIEZO);
    return;
  }

  if (!notFound && band == 0) {
    return;   // strong signal — stay silent, nothing to warn about
  }

  unsigned long interval;
  if (notFound) {
    interval = 150;   // worst case — most urgent, fastest chirp
  } else if (band == 1) {
    // Amber: interpolate 1200ms (just below the green threshold) down to
    // 500ms (right at the amber threshold).
    float t = (float)(avgRssi - RSSI_AMBER_THRESHOLD) /
              (float)(RSSI_GREEN_THRESHOLD - RSSI_AMBER_THRESHOLD);
    t = constrain(t, 0.0f, 1.0f);
    interval = 500 + (unsigned long)(t * (1200 - 500));
  } else {
    // Red: interpolate 500ms (right at the amber threshold) down to
    // 150ms (very poor, clamped at -90 dBm so it doesn't get faster
    // than the NOT_FOUND case above).
    int clamped = max(avgRssi, -90);
    float t = (float)(clamped - (-90)) / (float)(RSSI_AMBER_THRESHOLD - (-90));
    t = constrain(t, 0.0f, 1.0f);
    interval = 150 + (unsigned long)(t * (500 - 150));
  }

  if (millis() - lastBeepMs >= interval) {
    tone(PIEZO, 2000, 80);   // 80ms chirp at 2kHz
    lastBeepMs = millis();
  }
}

// Called once per loop() iteration in Mode 2. No-op unless a scan is in
// flight. Mirrors checkPendingScan() (Mode 1) but drives the traffic
// light / piezo / walk CSV instead of a floor-plan dot.
void checkAmbientScan() {
  if (!ambientScanPending) return;

  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;

  if (n == WIFI_SCAN_FAILED) {
    // The scan ATTEMPT itself failed at the driver level (an occasional,
    // known ESP32 WiFi quirk) -- this tells us nothing about whether the
    // target network is actually in range, unlike a scan that completes
    // normally and simply doesn't find it in the results. Previously this
    // was treated identically to a genuine "not found" (fed into the
    // history as RSSI_NOT_FOUND_STANDIN), which could drag a perfectly
    // good 3-reading average down into "poor signal" on a device that
    // hadn't moved at all, as soon as its very next scan completed fine.
    // So: skip this cycle entirely -- no history update, no redraw, no
    // CSV line -- and just let mode2Loop() retry on the next interval.
    Serial.println("Ambient scan failed (driver-level, not a real reading) -- skipping this cycle");
    WiFi.scanDelete();
    ambientScanPending = false;
    return;
  }

  Serial.print("Ambient scan complete (");
  Serial.print(millis() - ambientScanStartMs);
  Serial.print(" ms), ");
  Serial.print(n);
  Serial.println(" networks found");

  int channel;
  int rssi = bestRssiAndChannel(n, channel);
  int nearbyApCount = countNearbyApsOnChannel(n, channel);
  WiFi.scanDelete();
  ambientScanPending = false;

  // Roll this reading into the small history buffer used to smooth the
  // traffic light (a NOT_FOUND reading counts as a strong-negative
  // stand-in so "out of range" still pulls the average toward red).
  int valueForHistory = (rssi == RSSI_NOT_FOUND) ? RSSI_NOT_FOUND_STANDIN : rssi;
  if (rssiHistoryCount < RSSI_HISTORY_LEN) {
    rssiHistory[rssiHistoryCount++] = valueForHistory;
  } else {
    for (int i = 1; i < RSSI_HISTORY_LEN; i++) rssiHistory[i - 1] = rssiHistory[i];
    rssiHistory[RSSI_HISTORY_LEN - 1] = valueForHistory;
  }
  long sum = 0;
  for (int i = 0; i < rssiHistoryCount; i++) sum += rssiHistory[i];
  int avgRssi = sum / rssiHistoryCount;

  bool notFound = (rssi == RSSI_NOT_FOUND);   // this scan only, for the Serial line below
  int band;   // 0 = green, 1 = amber, 2 = red
  if (avgRssi >= RSSI_GREEN_THRESHOLD) band = 0;
  else if (avgRssi >= RSSI_AMBER_THRESHOLD) band = 1;
  else band = 2;

  // One shared "not found" verdict, used for the traffic light, the
  // piezo AND the CSV recommendation — a single dropout gets smoothed
  // away by the history average like any other reading (falls back to
  // the band above instead), and only counts as a firm "not found" when
  // there's no other data yet to smooth it against.
  bool notFoundForDisplay = notFound && rssiHistoryCount <= 1;

  currentBand = band;
  currentNotFound = notFoundForDisplay;
  currentAvgRssi = avgRssi;
  haveAmbientReading = true;

  if (band != lastDisplayedBand || app.currentZone != lastDisplayedZone) {
    drawTrafficLight(band, avgRssi, notFoundForDisplay);
    lastDisplayedBand = band;
    lastDisplayedZone = app.currentZone;
  }

  String recommendation = buildRecommendation(notFoundForDisplay, avgRssi, nearbyApCount, channel, 0);   // 0: Mode 2 has no full channel survey to recommend a specific channel from
  appendWalkCsvLine("SAMPLE", app.currentZone, rssi, avgRssi, recommendation);

  Serial.print("Ambient RSSI: ");
  if (notFound) Serial.print("not found"); else Serial.print(rssi);
  Serial.print(" (avg ");
  Serial.print(avgRssi);
  Serial.print(") -> ");
  Serial.println(recommendation);
}

void mode2Loop() {
  // --- Help button: show/hide the button reference screen ---
  if (digitalRead(BTN_HELP) == LOW) {
    showHelpScreen(2);
    return;
  }

  checkAmbientScan();

  if (!ambientScanPending && millis() - lastAmbientScanStartMs >= AMBIENT_SCAN_INTERVAL_MS) {
    WiFi.scanNetworks(true);
    ambientScanPending = true;
    ambientScanStartMs = millis();
    lastAmbientScanStartMs = millis();
  }

  if (haveAmbientReading) {
    updatePiezo(currentBand, currentNotFound, currentAvgRssi);
  }

  // --- Yellow button: mute/unmute piezo ---
  if (digitalRead(BTN_YELLOW) == LOW) {
    app.piezoMuted = !app.piezoMuted;
    Serial.println(app.piezoMuted ? "Piezo muted" : "Piezo unmuted");
    lastDisplayedBand = -1;   // force a redraw so the "Piezo: ..." line updates
    delay(200);
  }

  // --- Blue button: log a zone/room change ---
  if (digitalRead(BTN_BLUE) == LOW) {
    app.currentZone++;
    Serial.print("Zone changed to ");
    Serial.println(app.currentZone);
    // Build 7 item 5: explicit ZONE_CHANGE row (no RSSI/recommendation of
    // its own -- those come from the next SAMPLE row) so the zone boundary
    // is visible in the CSV itself, not just inferable from where the zone
    // column's value changes between SAMPLE rows.
    appendWalkCsvLine("ZONE_CHANGE", app.currentZone, RSSI_NOT_FOUND, 0, "");
    delay(200);
  }

  // --- Green button: back to the mode-select menu ---
  if (digitalRead(BTN_GREEN) == LOW) {
    waitForRelease(BTN_GREEN);
    if (ambientScanPending) {
      WiFi.scanDelete();
      ambientScanPending = false;
    }
    noTone(PIEZO);
    Serial.println("Returning to mode menu...");
    enterMode(selectMode());
  }
}

// ===========================================================================
// Mode 3: leave-in-place monitor
// ===========================================================================
// Unlike Modes 1/2 (which only ever scan, never associate), Mode 3
// connects to the target network with its password, so it can detect
// real disconnect/reconnect events, not just RSSI drift. The password
// is never hardcoded — it's entered once via the ESP32's built-in
// SoftAP provisioning, using the official "ESP SoftAP Prov" phone app
// (Espressif's own app, on iOS/Android), and stored by the ESP32 itself
// across reboots. Every subsequent boot just reconnects silently.

// Build 7 item 3: decodes the common WiFi disconnect reason codes (from
// esp_wifi_types.h, via WiFi.h) into a short, human-readable string for
// the monitor CSV. Only the well-documented, stable-across-core-versions
// codes are named here -- anything else (including codes that exist but
// aren't worth a special case, e.g. transient ones from normal roaming)
// falls back to just printing the numeric code, rather than guessing at
// less-common macro names that may not be defined in every core version.
const char* wifiDisconnectReasonToString(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:      return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_LEAVE:       return "AUTH_LEAVE";
    case WIFI_REASON_ASSOC_EXPIRE:     return "ASSOC_EXPIRE";
    case WIFI_REASON_ASSOC_TOOMANY:    return "ASSOC_TOOMANY (AP full/rejected)";
    case WIFI_REASON_NOT_AUTHED:       return "NOT_AUTHED";
    case WIFI_REASON_NOT_ASSOCED:      return "NOT_ASSOCED";
    case WIFI_REASON_BEACON_TIMEOUT:   return "BEACON_TIMEOUT (lost AP's beacon -- range/interference)";
    case WIFI_REASON_NO_AP_FOUND:      return "NO_AP_FOUND";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT (password/security mismatch, or weak signal)";
    default: {
      static char buf[24];   // static: the caller only uses this long enough to print/append it, same pattern as other small-formatting helpers in this sketch (e.g. currentTimestamp's internal buffer)
      snprintf(buf, sizeof(buf), "code %u", reason);
      return buf;
    }
  }
}

// WARNING (per Espressif's own docs): this is called from a separate
// FreeRTOS task, not the main loop — keep it quick, no SD/TFT access
// beyond simple flag-setting and Serial here, and do the actual CSV
// write from mode3Loop() instead. It's registered once in setup() and
// filters to Mode 3 itself, so Modes 1/2 (which also use WiFi, just for
// scanning) aren't affected by it.
void SysProvEvent(arduino_event_t *sys_event) {
  if (app.currentMode != 3) return;

  switch (sys_event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      // Build 7.5: push a queued event rather than just setting a shared
      // "reconnect vs fresh connect" flag -- see the Mode3EventEntry
      // comment near its declaration for why. wasReconnect is captured
      // right now, from whatever app.mode3State actually was the instant
      // before this GOT_IP -- still a plain read of a shared enum (same as
      // every other place in this sketch touches app.mode3State), just no
      // longer the only record of what happened.
      Mode3EventEntry entry;
      entry.type = MODE3_EVT_CONNECTED;
      entry.wasReconnect = (app.mode3State == MODE3_RECONNECTING);
      entry.duringChannelSurvey = mode3ScanPending;   // Build 7.5 item 2
      entry.atMs = millis();
      pushMode3Event(entry);
      app.mode3State = MODE3_CONNECTED;
      mode3NeedsNtpSync = true;   // actioned in mode3Loop() -- NTP sync must not run on this FreeRTOS task
      mode3NeedsWebServerStart = true;   // actioned in mode3Loop()
      Serial.print("Mode 3: connected, IP ");
      Serial.println(IPAddress(sys_event->event_info.got_ip.ip_info.ip.addr));
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      // Only treat this as a loggable event when we were actually
      // connected before — the initial connection attempt can fire
      // several of these while retrying, and those aren't real "drops".
      if (app.mode3State == MODE3_CONNECTED) {
        // Build 7 item 3: capture the driver's reason code the same way
        // the GOT_IP case above reads event_info.got_ip -- the union
        // member for this event is wifi_sta_disconnected, with a .reason
        // field (uint8_t, a wifi_err_reason_t value).
        app.mode3LastDisconnectReason = sys_event->event_info.wifi_sta_disconnected.reason;
        app.mode3HaveLastDisconnectReason = true;
        Serial.print("Mode 3: disconnected, reason ");
        Serial.println(app.mode3LastDisconnectReason);
        // Build 7.5: queue this transition (see above) instead of just
        // flagging "something changed" -- this is the actual event that
        // used to go missing when a reconnect followed it fast enough.
        Mode3EventEntry entry;
        entry.type = MODE3_EVT_DISCONNECTED;
        entry.disconnectReason = app.mode3LastDisconnectReason;
        entry.hadReason = true;
        entry.duringChannelSurvey = mode3ScanPending;   // Build 7.5 item 2
        entry.atMs = millis();
        pushMode3Event(entry);
        // Build 7 item 1: don't drop straight to MODE3_OFFLINE -- give
        // auto-reconnect a short grace window (mode3Loop() demotes this to
        // MODE3_OFFLINE and logs a distinct sustained-loss event if it
        // doesn't resolve within MODE3_RECONNECT_GRACE_MS) so a brief blip
        // doesn't read identically to a genuine outage.
        app.mode3State = MODE3_RECONNECTING;
        mode3ReconnectingSinceMs = millis();
      }
      break;
    case ARDUINO_EVENT_PROV_START:
      Serial.println("Mode 3: provisioning started — use the 'ESP SoftAP Prov' app");
      break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
      Serial.print("Mode 3: received Wi-Fi credentials for ");
      Serial.println((const char *)sys_event->event_info.prov_cred_recv.ssid);
      break;
    case ARDUINO_EVENT_PROV_CRED_FAIL:
      Serial.println("Mode 3: provisioning failed — check the Wi-Fi password and try again");
      break;
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      Serial.println("Mode 3: provisioning successful");
      break;
    case ARDUINO_EVENT_PROV_END:
      Serial.println("Mode 3: provisioning ended");
      break;
    default:
      break;
  }
}

// Appends one line to MONITOR_LOG_FILE — event is "SAMPLE", "CONNECTED"
// or "DISCONNECTED"; rssi is RSSI_NOT_FOUND when not meaningful (e.g. a
// DISCONNECTED row). recommendation is plain-English advice (only
// meaningful alongside a real RSSI reading — pass "" for CONNECTED/
// DISCONNECTED rows, which just mark an event with no reading to judge).
//
// NOTE: if you already have an existing /monitor_log.csv from before this
// column was added, delete or rename it on the SD card first — the header
// is only written once, when the file doesn't exist yet, so an old file
// would keep its old 3-column header while new rows have 4 columns.
// Internal-flash fallback for the Mode 3 monitor log (Build 1). SD stays
// the primary copy — easy to pull the card and read on a PC — this file
// only exists to catch a line that would otherwise be silently lost if
// the card drops out mid-session (contact bounce, card worked loose,
// write error) after a perfectly good init at boot.
void appendMonitorLogLineToFallback(const char *event, int rssi, const String &recommendation) {
  bool fileExists = FFat.exists(MONITOR_LOG_FALLBACK_FILE);
  File f = FFat.open(MONITOR_LOG_FALLBACK_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("Mode 3: internal-flash fallback log also failed to open — line lost");
    return;
  }
  if (!fileExists) {
    f.println("sequence,storage,timestamp,event,rssi_dbm,recommendation");
  }
  globalLogSequence++;
  f.print(globalLogSequence);
  f.print(",FFAT,");
  f.print(currentTimestamp());
  f.print(",");
  f.print(event);
  f.print(",");
  if (rssi == RSSI_NOT_FOUND) {
    f.print("");
  } else {
    f.print(rssi);
  }
  f.print(",");
  f.println(recommendation);
  f.close();
  Serial.println("Mode 3: SD write failed — line saved to internal flash fallback instead (/monitor_log_fallback.csv)");
}

void appendMonitorLogLine(const char *event, int rssi, const String &recommendation) {
  // Internal flash preferred (chosen at boot) -- same as Mode 2's
  // appendWalkCsvLine, always use it directly, no need to ever try SD.
  if (preferInternalLogging) {
    appendMonitorLogLineToFallback(event, rssi, recommendation);
    return;
  }

  // SD preferred (the default) -- same live choice as Mode 2's
  // appendWalkCsvLine -- SD if it's actually there right now, internal
  // flash if not, re-checked every line rather than fixed once at boot.
  if (!app.sdCardAvailable) {
    app.sdCardAvailable = trySDInit();
  }
  if (!app.sdCardAvailable) {
    appendMonitorLogLineToFallback(event, rssi, recommendation);
    return;
  }

  bool fileExists = SD.exists(MONITOR_LOG_FILE);
  File f = SD.open(MONITOR_LOG_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("Could not open monitor log CSV for append -- card may have been removed");
    app.sdCardAvailable = false;
    appendMonitorLogLineToFallback(event, rssi, recommendation);
    return;
  }
  if (!fileExists) {
    f.println("sequence,storage,timestamp,event,rssi_dbm,recommendation");
  }
  globalLogSequence++;
  f.print(globalLogSequence);
  f.print(",SD,");
  f.print(currentTimestamp());
  f.print(",");
  f.print(event);
  f.print(",");
  if (rssi == RSSI_NOT_FOUND) {
    f.print("");
  } else {
    f.print(rssi);
  }
  f.print(",");
  f.println(recommendation);
  f.close();
}

// Build 7 item 6: only .csv/.txt files (case-insensitive) are ever listed
// or downloadable through the web server -- this device's SD/flash only
// ever holds floor-plan JPEGs and log CSVs, but restricting the file
// server to the log-file extensions it's actually meant for means a JPEG
// (or anything else that ends up on the card) can't be listed or pulled
// through it either, belt-and-suspenders with the path-traversal check
// below rather than relying on either check alone.
bool isDownloadableFilename(const String &name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".csv") || lower.endsWith(".txt");
}

// Lists every file on SD and on the internal-flash (FFat) fallback, each as
// a download link. No styling to speak of -- this is a utility page for
// grabbing logs off the device, not something to look pretty.
void handleFileListPage() {
  String html;
  html.reserve(4096);   // Build 7.5: this is the one place in the sketch building a non-trivial String through repeated concatenation -- reserving up front avoids the repeated grow-and-copy reallocations that are the actual fragmentation risk, for the cost of one line
  html = "<html><head><title>Wi-FEye files</title></head><body>";
  html += "<h2>Wi-FEye log files</h2>";

  html += "<h3>SD Card</h3><ul>";
  if (app.sdCardAvailable) {
    File root = SD.open("/");
    File entry = root.openNextFile();
    bool any = false;
    while (entry) {
      if (!entry.isDirectory() && isDownloadableFilename(String(entry.name()))) {
        any = true;
        String name = String(entry.name());
        if (!name.startsWith("/")) name = "/" + name;
        html += fileListRowHtml(name, entry.size(), "sd");
      }
      entry.close();
      entry = root.openNextFile();
    }
    root.close();
    if (!any) html += "<li>(no files)</li>";
  } else {
    html += "<li>(no SD card detected)</li>";
  }
  html += "</ul>";

  html += "<h3>Internal Flash (fallback logs)</h3><ul>";
  File ffatRoot = FFat.open("/");
  File ffatEntry = ffatRoot.openNextFile();
  bool anyFallback = false;
  while (ffatEntry) {
    if (!ffatEntry.isDirectory() && isDownloadableFilename(String(ffatEntry.name()))) {
      anyFallback = true;
      String name = String(ffatEntry.name());
      if (!name.startsWith("/")) name = "/" + name;
      html += fileListRowHtml(name, ffatEntry.size(), "ffat");
    }
    ffatEntry.close();
    ffatEntry = ffatRoot.openNextFile();
  }
  ffatRoot.close();
  if (!anyFallback) html += "<li>(no files)</li>";
  html += "</ul></body></html>";

  fileServer.send(200, "text/html", html);
}

// Build 7.2: one file's <li> row -- the download link plus a small inline
// Delete form (POST, with a JS confirm() so a stray tap/click can't wipe
// a log by accident). A bare <a href="/delete?..."> would also let a
// browser's link-prefetching or a crawler trigger the delete just by
// visiting the page, which a <form method="POST"> avoids.
String fileListRowHtml(const String &name, size_t sizeBytes, const char *fsArg) {
  String row = "<li><a href=\"/download?fs=" + String(fsArg) + "&file=" + name + "\">" + name + "</a> (" + String((unsigned long)sizeBytes) + " bytes) ";
  row += "<form method=\"POST\" action=\"/delete\" style=\"display:inline\" onsubmit=\"return confirm('Delete " + name + "? This cannot be undone.');\">";
  row += "<input type=\"hidden\" name=\"fs\" value=\"" + String(fsArg) + "\">";
  row += "<input type=\"hidden\" name=\"file\" value=\"" + name + "\">";
  row += "<button type=\"submit\">Delete</button>";
  row += "</form></li>";
  return row;
}

// Deletes one file, then redirects back to "/" so the list page refreshes
// and shows it gone. Same trust model as the rest of this server (no
// auth, home-network-only, minimal path checks) -- see handleFileDownload().
void handleFileDelete() {
  if (!fileServer.hasArg("file") || !fileServer.hasArg("fs")) {
    fileServer.send(400, "text/plain", "Missing file or fs parameter");
    return;
  }
  String path = fileServer.arg("file");
  String fsArg = fileServer.arg("fs");

  if (path.indexOf("..") >= 0 || !path.startsWith("/")) {
    fileServer.send(400, "text/plain", "Invalid path");
    return;
  }
  if (!isDownloadableFilename(path)) {
    fileServer.send(400, "text/plain", "Only .csv/.txt files can be deleted");
    return;
  }

  bool ok;
  if (fsArg == "sd") {
    ok = SD.remove(path);
  } else if (fsArg == "ffat") {
    ok = FFat.remove(path);
  } else {
    fileServer.send(400, "text/plain", "Unknown filesystem");
    return;
  }

  if (!ok) {
    fileServer.send(500, "text/plain", "Delete failed (file in use or already gone?)");
    return;
  }

  Serial.print("File Browser: deleted "); Serial.println(path);
  fileServer.sendHeader("Location", "/");
  fileServer.send(303);   // redirect back to the file list
}

// Streams one file back as a download. No authentication -- this server
// is meant for your own trusted home network only, same spirit as the
// open SoftAP Prov setup. The ".." check is a minimal safety net against
// an obviously malformed request, not real path sandboxing.
void handleFileDownload() {
  if (!fileServer.hasArg("file") || !fileServer.hasArg("fs")) {
    fileServer.send(400, "text/plain", "Missing file or fs parameter");
    return;
  }
  String path = fileServer.arg("file");
  String fsArg = fileServer.arg("fs");

  if (path.indexOf("..") >= 0 || !path.startsWith("/")) {
    fileServer.send(400, "text/plain", "Invalid path");
    return;
  }
  // Build 7 item 6: defense in depth -- don't rely on the listing page
  // alone to keep non-log files out of reach; reject a direct request
  // for anything that isn't a .csv/.txt file too.
  if (!isDownloadableFilename(path)) {
    fileServer.send(400, "text/plain", "Only .csv/.txt files can be downloaded");
    return;
  }

  File f;
  if (fsArg == "sd") {
    f = SD.open(path, FILE_READ);
  } else if (fsArg == "ffat") {
    f = FFat.open(path, FILE_READ);
  } else {
    fileServer.send(400, "text/plain", "Unknown filesystem");
    return;
  }

  if (!f) {
    fileServer.send(404, "text/plain", "File not found");
    return;
  }

  String downloadName = path.substring(path.lastIndexOf('/') + 1);
  fileServer.sendHeader("Content-Disposition", "attachment; filename=\"" + downloadName + "\"");
  fileServer.streamFile(f, "application/octet-stream");
  f.close();
}

// Build 7 item 2: redraws the normal status screen UNLESS a non-blocking
// overlay (Help or File-access) is currently showing, in which case this
// is a no-op -- the overlay stays on screen and whatever state change just
// happened (now logged either way) will be reflected next time the
// overlay is dismissed, same as the status screen was already only ever a
// snapshot repainted on the next real event. Use this everywhere mode3Loop()
// used to call drawMode3Status() directly for an in-loop state change.
void refreshMode3Screen() {
  if (app.mode3HelpOverlayActive || app.mode3FileAccessOverlayActive) return;
  drawMode3Status();
}

// Body text bumped from size 1 to size 2 (larger-font request) -- lines
// reworded/shortened where needed since size 2 only fits ~26 characters
// before running off the right edge (Adafruit_GFX doesn't wrap). The IP
// address used to live on this screen; it's now its own on-demand screen
// (Yellow -- see showFileAccessScreen()) so this one stays uncluttered.
void drawMode3Status() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Mode 3: Monitor");

  tft.setCursor(10, 40);
  if (app.mode3State == MODE3_CONNECTED) {
    tft.setTextColor(ILI9341_GREEN);
    tft.println("Connected");
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(10, 58);
    tft.print("RSSI: ");
    tft.print(WiFi.RSSI());
    tft.println(" dBm");
  } else if (app.mode3State == MODE3_RECONNECTING) {
    // Build 7: distinct sub-state so a brief drop that's about to resolve
    // on its own doesn't look identical to "never set up yet" -- see
    // mode3Loop()'s MODE3_RECONNECT_GRACE_MS handling.
    tft.setTextColor(ILI9341_ORANGE);
    tft.println("Reconnecting...");
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(10, 58);
    tft.println("Lost connection -- retrying.");
  } else {
    tft.setTextColor(ILI9341_ORANGE);
    tft.println("Waiting for setup...");
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(10, 58);
    tft.println("Not yet set up:");
    tft.setCursor(10, 76);
    tft.println("Open 'ESP SoftAP Prov'");
    tft.setCursor(10, 94);
    tft.println("app, connect to:");
    tft.setCursor(10, 112);
    tft.setTextColor(ILI9341_YELLOW);
    tft.print("  ");
    tft.println(MODE3_SERVICE_NAME);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(10, 130);
    tft.print("  PoP: ");
    tft.println(MODE3_POP);
  }

  tft.setCursor(10, 152);
  tft.setTextColor(mode3AlertPiezoEnabled ? ILI9341_WHITE : ILI9341_DARKGREY);
  tft.print("Alert piezo: ");
  tft.println(mode3AlertPiezoEnabled ? "ON" : "OFF");

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(10, SCREEN_H - 52);
  tft.println("Hold Blue 2s: forget network");
  tft.setCursor(10, SCREEN_H - 40);
  tft.println("Yellow: show file address");
  tft.setCursor(10, SCREEN_H - 28);
  tft.println("Red: toggle alert piezo");
  tft.setCursor(10, SCREEN_H - 16);
  tft.println("Green: back to mode menu");
}

// New screen (Yellow, Build 2.1) -- shows the device's IP and the file
// server's URL so you can grab logs from your phone/laptop without
// pulling the SD card. Only meaningful once Connected; says so plainly
// otherwise rather than printing a useless 0.0.0.0.
// Build 7 item 2: now non-blocking, same as showHelpScreen()'s Mode 3
// path -- draws once, marks the overlay active, and returns immediately;
// mode3Loop() polls for the dismiss press/timeout so WiFi events, logging
// and the web server keep running while this is shown.
void showFileAccessScreen() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("File access");

  if (app.mode3State == MODE3_CONNECTED) {
    tft.setCursor(10, 50);
    tft.println("Browse to:");
    tft.setTextColor(ILI9341_YELLOW);
    tft.setCursor(10, 76);
    tft.print("http://");
    tft.println(WiFi.localIP());
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(10, 102);
    tft.println("on a device on the");
    tft.setCursor(10, 120);
    tft.println("same network.");
  } else {
    tft.setTextColor(ILI9341_ORANGE);
    tft.setCursor(10, 50);
    tft.println("Not connected yet --");
    tft.setCursor(10, 68);
    tft.println("no address to show.");
  }

  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(10, SCREEN_H - 15);
  tft.println("Press Yellow to return.");

  waitForRelease(BTN_YELLOW);   // the press that opened this screen is still being held
  app.mode3FileAccessOverlayActive = true;
  app.mode3FileAccessOverlayShownMs = millis();
}

// Non-blocking beeper for an active disconnect alert -- a simple fixed,
// urgent chirp (unlike Mode 2's updatePiezo(), there's no "band" to
// interpolate here; this only ever means one thing: you've dropped).
// Silenced the moment mode3AlertActive goes false, whether that's from a
// button press or a reconnect -- see mode3Loop().
void updateMode3Alert() {
  static unsigned long lastAlertBeepMs = 0;
  if (millis() - lastAlertBeepMs >= 400) {
    tone(PIEZO, 2200, 150);
    lastAlertBeepMs = millis();
  }
}

void startMode3() {
  drawMode3Status();

  if (!mode3Started) {
    mode3Started = true;
    app.mode3State = MODE3_CONNECTING;   // Build 7: provisioning/connect kicked off below -- see Mode3ConnectionState
    mode3LastCongestionCheckMs = millis();
    mode3NearbyApCount = 0;
    mode3CongestionChannel = -1;
    blueHoldStartMs = 0;
    mode3AlertActive = false;   // leftover from a previous Mode 3 session this boot shouldn't carry over
    noTone(PIEZO);

    // Fresh running-summary stats for this monitoring session
    mode3MonitorStartMs = millis();
    mode3LastStateChangeMs = mode3MonitorStartMs;
    mode3ConnectedAccumMs = 0;
    mode3DropCount = 0;
    mode3RssiSum = 0;
    mode3RssiSampleCount = 0;
    mode3LastKnownRssi = RSSI_NOT_FOUND;

    // Reprovisioning is handled entirely by forgetMode3NetworkAndRestart()
    // (hold Blue for BLUE_FORGET_HOLD_MS at any time while in Mode 3) —
    // that erases the stored credentials and restarts, so beginProvision()
    // always sees a clean "not provisioned" state on the next boot and
    // opens the SoftAP setup fresh. We deliberately never pass
    // reset_provisioned=true here: the underlying provisioning manager
    // only honors that flag once per boot (a library quirk confirmed by
    // reading WiFiProv.cpp), which made reprovisioning only work if Blue
    // was held before Mode 3's very first entry after a fresh power-up —
    // easy to miss and impossible to trigger once already in Mode 3.
    // Build 7.1: dropped the WiFi.begin() that used to sit right here.
    // Serial logs caught "E (...) wifi:sta is connecting, cannot set
    // config" firing every time Mode 3 started, right between that
    // WiFi.begin() and beginProvision() below -- WiFi.begin() put the STA
    // radio into a connecting state, and beginProvision()'s own attempt to
    // configure the driver (to bring up the SoftAP) was then rejected
    // because of it. beginProvision() already connects with stored
    // credentials on its own when already provisioned, and opens the
    // SoftAP when not -- WiFi.begin() here was redundant as well as the
    // direct cause of the conflict, so it's gone rather than reordered.
    WiFi.setAutoReconnect(true);
    WiFiProv.beginProvision(
      NETWORK_PROV_SCHEME_SOFTAP, NETWORK_PROV_SCHEME_HANDLER_NONE,
      NETWORK_PROV_SECURITY_1, MODE3_POP, MODE3_SERVICE_NAME, NULL, NULL, false
    );
  }

  mode3LastSampleMs = millis();
}

// Erases the stored Wi-Fi credentials (the same ones network_prov_mgr_is_
// wifi_provisioned() checks) and restarts the device. On the next boot,
// Mode 3 sees a clean, never-provisioned state and beginProvision() opens
// the SoftAP setup screen again — so the "ESP SoftAP Prov" app can be used
// to enter a *different* network, not just re-enter the same one. This
// works no matter when it's triggered (right after boot, mid-session,
// already connected for days), unlike the old first-entry-only mechanic.
void forgetMode3NetworkAndRestart() {
  Serial.println("Mode 3: Blue held — forgetting stored network and restarting...");
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_ORANGE);
  tft.setTextSize(2);
  tft.setCursor(10, 100);
  tft.println("Forgetting network...");
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 130);
  tft.println("Restarting - reconnect via 'ESP SoftAP Prov' app.");
  // Build 7.1: the same "wifi:sta is connecting, cannot set config" race
  // fixed in startMode3() also showed up right here -- if the STA radio
  // is still mid-connect (beginProvision() reconnecting with the stored
  // credentials, or its own internal retry) at the exact moment this runs,
  // the erase below can be rejected by the driver and silently do nothing,
  // leaving the old credentials in place -- which would explain a forget
  // that reboots the device but still doesn't open a fresh SoftAP
  // afterward (beginProvision() on the next boot still sees itself as
  // provisioned).
  //
  // Build 7.1 round 2: a plain disconnect(false,false) + 300ms delay was
  // tried first and did NOT hold -- the same "cannot set config" error
  // still fired at the erase call below. Most likely reason: startMode3()
  // turns on WiFi.setAutoReconnect(true), which keeps retrying the stored
  // credentials in the background. A plain disconnect() only cancels
  // *that one* attempt -- autoReconnect can immediately kick off another,
  // so by the time the erase call ran the STA was often back in a
  // "connecting" state. Fix: turn autoReconnect off first (removes the
  // thing that keeps putting the radio back into that state), then also
  // fully power the STA driver off and back on as a belt-and-braces step
  // -- that forces the driver out of *any* in-progress state (connecting,
  // reconnecting, scanning), rather than relying on disconnect() alone
  // having caught up with whatever it was doing.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  delay(300);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(200);
  WiFi.disconnect(true, true);   // wifioff=true, eraseAP=true: clears the stored SSID/password from flash
  delay(500);                    // give the erase + Serial print time to actually complete before reset
  ESP.restart();
}

// Build 7.6: recognizes sibling BSSIDs -- same physical AP radio as the
// one we're connected to, broadcasting a different SSID -- so they don't
// get double-counted as separate competing networks. Confirmed during
// field testing across multiple rooms/APs: this site's enterprise AP
// deployment has every physical radio broadcasting several SSIDs at once
// (e.g. CFG_Team/CFG_Guest/CFG_RZ/Zorba/Major-Clks all sharing one base
// MAC, differing only in the last octet), which is normal, intentional AP
// configuration, not real channel contention. Matches on the first 5
// octets (vendor+device prefix) of the BSSID -- sibling radios from the
// same hardware share those, differing only in the last byte.
bool isSiblingRadio(int i, const uint8_t *myBssid) {
  if (myBssid == nullptr) return false;
  uint8_t *bssid = WiFi.BSSID(i);
  if (bssid == nullptr) return false;
  for (int o = 0; o < 5; o++) {
    if (bssid[o] != myBssid[o]) return false;
  }
  return true;
}

// Like countNearbyApsOnChannel(), but excludes whatever network Mode 3 is
// actually connected to rather than the hardcoded TARGET_SSID — Mode 3
// connects to whatever SSID/password was entered via the SoftAP Prov app,
// which won't always be TARGET_SSID (that constant is Modes 1/2's fixed
// survey target). Without this, our own AP's beacon showing up in the
// scan would get miscounted as a "competing" network on its own channel.
// Build 7.6: also excludes sibling BSSIDs (see isSiblingRadio() above) so
// a single AP broadcasting several SSIDs isn't counted several times.
int countMode3NearbyApsOnChannel(int n, int targetChannel, const uint8_t *myBssid) {
  if (targetChannel <= 0) return 0;
  String mySSID = WiFi.SSID();
  int count = 0;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == mySSID) continue;
    if (isSiblingRadio(i, myBssid)) continue;
    if (WiFi.channel(i) == targetChannel && WiFi.RSSI(i) > -80) count++;
  }
  return count;
}

// Build 1: flags a disconnect that doesn't fit the usual "weak signal" or
// "crowded channel" explanations — those are the only two causes Modes
// 1-3 can otherwise point to, since a plain WiFi scan can't see non-WiFi
// 2.4GHz sources (microwaves, Bluetooth, baby monitors, etc.). If the
// signal was strong and the channel wasn't congested right before the
// drop, something else was almost certainly responsible, so this adds
// that as a candidate explanation rather than leaving the DISCONNECTED
// row with no recommendation at all. lastRssi is the most recent SAMPLE
// reading before the drop (RSSI_NOT_FOUND if we never got one this
// session); nearbyApCount is from the last completed congestion check.
String buildInterferenceNote(int lastRssi, int nearbyApCount) {
  if (lastRssi != RSSI_NOT_FOUND && lastRssi >= RSSI_GREEN_THRESHOLD && nearbyApCount < 2) {
    // Build 7: softened wording -- the old phrasing led with "non-WiFi
    // interference" as if it were the diagnosis; a plain WiFi scan can't
    // actually see 2.4GHz non-WiFi sources (or most router-side/client
    // quirks) at all, so this is one candidate among several, not a verdict.
    return "Disconnect occurred despite strong signal and low detected Wi-Fi congestion. Possible causes include router-side issues, client/AP compatibility, or non-Wi-Fi interference.";
  }
  return "";
}

// Polls an in-flight congestion scan (kicked off from mode3Loop() every
// MODE3_CONGESTION_CHECK_INTERVAL_MS) and, once it completes, updates
// mode3NearbyApCount/mode3CongestionChannel from the results. Mirrors
// checkPendingScan()/checkAmbientScan() — never blocks.
// Build 2: attempts to sync the RTC from NTP. Called from mode3Loop()
// (never from SysProvEvent() itself -- that runs on a separate FreeRTOS
// task and blocking network calls there are unsafe) right after a fresh
// connection. Bounded by a short timeout so an unreachable NTP server
// can't hang mode3Loop() for long -- the RTC's own coin-cell backup is
// the authoritative clock the rest of the time; this is just an
// opportunistic correction whenever Mode 3 happens to be online. Logs in
// UTC (no DST handling) for simplicity and consistency across sessions.
void syncRtcFromNtp() {
  if (!app.rtcAvailable) return;
  Serial.println("Mode 3: attempting NTP time sync...");
  // Build 7 item 5: capture the pre-sync timestamp before touching the RTC
  // so the TIME_SYNC row below can note old -> new, making a sudden jump
  // in later timestamps traceable back to a specific correction rather
  // than looking like a gap or a clock glitch.
  String oldTimestamp = currentTimestamp();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000)) {
    time_t epoch = mktime(&timeinfo);
    rtc.adjust(DateTime((uint32_t)epoch));
    Serial.println("Mode 3: RTC synced from NTP (UTC)");
    String newTimestamp = currentTimestamp();
    appendMonitorLogLine("TIME_SYNC", RSSI_NOT_FOUND, "RTC corrected from NTP: " + oldTimestamp + " -> " + newTimestamp);
  } else {
    Serial.println("Mode 3: NTP sync failed (no response within timeout) -- RTC left as-is");
  }
}

// Build 7 item 4: formats up to the 5 strongest same-channel neighbors
// found in a completed scan as "SSID|BSSID|RSSI|channel" entries separated
// by "; ", for a NEARBY_APS log row -- capped so a crowded channel doesn't
// blow up the CSV, and a hidden (empty) SSID is shown as "<hidden>" rather
// than an empty, CSV-parser-confusing field.
// Build 7.6: also takes myBssid so sibling radios (see isSiblingRadio()
// above) are excluded here too -- otherwise a NEARBY_APS row would still
// list an AP's own other SSIDs as if they were separate neighbors, even
// after the count itself was fixed.
String buildNearbyApSummary(int n, int targetChannel, const String &mySSID, const uint8_t *myBssid) {
  if (targetChannel <= 0) return "";
  const int MAX_LOGGED = 5;
  int keptIdx[MAX_LOGGED];
  int keptRssi[MAX_LOGGED];
  int keptCount = 0;

  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == mySSID) continue;
    if (isSiblingRadio(i, myBssid)) continue;
    if (WiFi.channel(i) != targetChannel) continue;
    int rssi = WiFi.RSSI(i);
    if (keptCount < MAX_LOGGED) {
      keptIdx[keptCount] = i;
      keptRssi[keptCount] = rssi;
      keptCount++;
    } else {
      int weakestPos = 0;
      for (int k = 1; k < MAX_LOGGED; k++) {
        if (keptRssi[k] < keptRssi[weakestPos]) weakestPos = k;
      }
      if (rssi > keptRssi[weakestPos]) {
        keptIdx[weakestPos] = i;
        keptRssi[weakestPos] = rssi;
      }
    }
  }

  // Small insertion sort, strongest first -- keptCount is at most 5.
  for (int a = 1; a < keptCount; a++) {
    int ti = keptIdx[a], tr = keptRssi[a];
    int b = a - 1;
    while (b >= 0 && keptRssi[b] < tr) {
      keptIdx[b + 1] = keptIdx[b];
      keptRssi[b + 1] = keptRssi[b];
      b--;
    }
    keptIdx[b + 1] = ti;
    keptRssi[b + 1] = tr;
  }

  String out = "";
  for (int k = 0; k < keptCount; k++) {
    String ssid = WiFi.SSID(keptIdx[k]);
    if (ssid.length() == 0) ssid = "<hidden>";
    if (k > 0) out += "; ";
    out += ssid + "|" + WiFi.BSSIDstr(keptIdx[k]) + "|" + String(keptRssi[k]) + "|" + String(targetChannel);
  }
  return out;
}

// Build 7.1: countMode3NearbyApsOnChannel() / buildNearbyApSummary() only
// ever look at the channel Mode 3 is already on, so the most they can say
// is "your channel is busy" -- they can't name a better one. This tallies
// every 2.4GHz channel (1-13) seen in the same completed scan and picks
// the quietest of the three non-overlapping 20MHz channels (1, 6, 11 --
// same convention in the UK/EU as the US), so the DIAGNOSTICS screen and
// CSV can make an actual "switch to channel X" suggestion. Deliberately
// kept inside the existing monitor-log CSV schema (no new column) by
// packing it into its own CHANNEL_SURVEY row's note field, the same way
// NEARBY_APS already does -- see checkMode3CongestionScan() below.
//
// Important limits, stated plainly rather than left implicit: this is
// still only a count of *other visible Wi-Fi networks* above -80dBm per
// channel, not actual traffic/airtime utilization, and the ESP32-S3's
// radio is 2.4GHz only -- it cannot see a 5GHz network at all, even one
// broadcast by the same router right next to the one being measured.
// Build 7.6: also takes myBssid so the per-channel tally excludes sibling
// radios (see isSiblingRadio() above), not just an exact SSID match --
// otherwise a single AP's other SSIDs still inflated the count on
// whichever channel it sits on, even on channels other than our own.
String buildChannelSurvey(int n, int currentChannel, int *outRecommended, const uint8_t *myBssid) {
  String mySSID = WiFi.SSID();
  int counts[14] = {0};   // index 1..13 used; 0 left unused for readability
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == mySSID) continue;
    if (isSiblingRadio(i, myBssid)) continue;
    int ch = WiFi.channel(i);
    if (ch < 1 || ch > 13) continue;
    if (WiFi.RSSI(i) > -80) counts[ch]++;
  }

  // Build 7.5: tie-break fix. This used to always start from channel 1 and
  // only move off it on a strict "<" comparison -- which sounds fine, but
  // because the starting point was always 1 rather than whatever's
  // actually in use, a 3-way tie (e.g. all of 1/6/11 equally quiet) always
  // "recommended" switching to 1 even if you were already sitting on an
  // equally-quiet 6 or 11 -- advice with no real benefit behind it. Now it
  // starts from the current channel (if it's one of the three candidates;
  // falls back to 1 if not) and only moves away when another candidate is
  // STRICTLY quieter, so a tie keeps the recommendation as "stay put".
  int best = (currentChannel == 1 || currentChannel == 6 || currentChannel == 11) ? currentChannel : 1;
  const int nonOverlapping[3] = {1, 6, 11};
  for (int k = 0; k < 3; k++) {
    if (counts[nonOverlapping[k]] < counts[best]) best = nonOverlapping[k];
  }
  if (outRecommended) *outRecommended = best;

  String out = "";
  for (int ch = 1; ch <= 13; ch++) {
    if (ch > 1) out += " ";
    out += "ch" + String(ch) + "=" + String(counts[ch]);
  }
  out += " current=" + String(currentChannel);
  out += " recommended=" + String(best);
  if (best == currentChannel) {
    out += " (already quietest of 1/6/11)";
  }
  out += " -- visible-AP counts only (>-80dBm), 2.4GHz only, not traffic volume or non-WiFi interference";
  return out;
}

void checkMode3CongestionScan() {
  if (!mode3ScanPending) return;

  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;

  if (n == WIFI_SCAN_FAILED) {
    Serial.println("Mode 3: congestion scan failed");
  } else {
    int channel = WiFi.channel();   // current AP's channel — already connected, no need to find it in the scan
    // Build 7.6: copied into a local array (rather than passing WiFi.BSSID()'s
    // pointer straight through) so it stays valid across the several calls
    // below regardless of what WiFi.* internally does between them.
    uint8_t myBssid[6];
    memcpy(myBssid, WiFi.BSSID(), 6);
    mode3CongestionChannel = channel;
    mode3NearbyApCount = countMode3NearbyApsOnChannel(n, channel, myBssid);
    mode3NearbyApSummary = buildNearbyApSummary(n, channel, WiFi.SSID(), myBssid);
    Serial.print("Mode 3: congestion check — channel ");
    Serial.print(channel);
    Serial.print(", ");
    Serial.print(mode3NearbyApCount);
    Serial.println(" competing network(s) on it (sibling SSIDs on our own AP excluded)");
    // Build 7 item 4: its own row, logged once per completed scan (every
    // MODE3_CONGESTION_CHECK_INTERVAL_MS while connected) rather than on
    // every SAMPLE row, so detailed per-neighbor data doesn't bloat the
    // common case.
    if (mode3NearbyApSummary.length() > 0) {
      appendMonitorLogLine("NEARBY_APS", RSSI_NOT_FOUND, mode3NearbyApSummary);
    }
    // Build 7.1: the full per-channel tally -- see buildChannelSurvey()'s
    // own comment for what this can and can't tell you.
    String channelSurvey = buildChannelSurvey(n, channel, &mode3RecommendedChannel, myBssid);
    appendMonitorLogLine("CHANNEL_SURVEY", RSSI_NOT_FOUND, channelSurvey);
  }
  WiFi.scanDelete();
  mode3ScanPending = false;
}

// Build 7.5: turns the "heavy String usage could fragment the heap over
// days/weeks" concern from a theoretical worry into something actually
// observable. Checked on the same cadence as the channel-congestion scan
// (cheap, infrequent -- no reason to call ESP.getFreeHeap() every loop()
// iteration) rather than tied to its own timer, since both are "every so
// often while connected" housekeeping of the same kind.
//
// Always prints to Serial (cheap, and useful while watching it live
// during a test); only writes a CSV row when free heap actually drops
// below the watch threshold, so a healthy device running for weeks
// doesn't quietly fill its own log with "all fine" rows.
void checkMode3HeapHealth() {
  if (millis() - mode3LastHeapCheckMs < MODE3_CONGESTION_CHECK_INTERVAL_MS) return;
  mode3LastHeapCheckMs = millis();

  uint32_t freeHeap = ESP.getFreeHeap();
  Serial.print("Mode 3: free heap ");
  Serial.print(freeHeap);
  Serial.println(" bytes");

  if (freeHeap < MODE3_LOW_HEAP_THRESHOLD_BYTES) {
    String note = "Free heap " + String(freeHeap) + " bytes -- below the " +
                  String(MODE3_LOW_HEAP_THRESHOLD_BYTES) + "-byte watch threshold. " +
                  "Not necessarily a fault on its own, but worth noting if it keeps falling on successive checks.";
    Serial.print("Mode 3: WARNING -- ");
    Serial.println(note);
    appendMonitorLogLine("LOW_HEAP", RSSI_NOT_FOUND, note);
  }
}

void mode3Loop() {
  fileServer.handleClient();

  // Silence an active disconnect alert the instant any button is pressed,
  // without blocking that button's own normal action below -- Help still
  // opens its screen, Green still leaves Mode 3, Red still toggles the
  // setting. The alert is just meant to get your attention, not demand a
  // specific response.
  if (mode3AlertActive &&
      (digitalRead(BTN_YELLOW) == LOW || digitalRead(BTN_BLUE) == LOW ||
       digitalRead(BTN_GREEN) == LOW || digitalRead(BTN_RED) == LOW ||
       digitalRead(BTN_HELP) == LOW)) {
    mode3AlertActive = false;
    noTone(PIEZO);
  }

  // --- Build 7 item 2: Help/File-access overlays are now non-blocking --
  // showHelpScreen(3)/showFileAccessScreen() just draw once and set their
  // "active" flag (see below), so every bit of Mode 3 housekeeping further
  // down this function (WiFi event handling, the web server, periodic
  // logging, the congestion scan) keeps running underneath them instead of
  // freezing for as long as the screen is up. The ONLY thing an active
  // overlay suppresses is the normal drawMode3Status() redraw -- see
  // refreshMode3Screen() below.
  if (digitalRead(BTN_HELP) == LOW) {
    if (app.mode3HelpOverlayActive) {
      waitForRelease(BTN_HELP);
      app.mode3HelpOverlayActive = false;
      drawMode3Status();
    } else if (!app.mode3FileAccessOverlayActive) {
      showHelpScreen(3);
    }
  }
  if (digitalRead(BTN_YELLOW) == LOW) {
    if (app.mode3FileAccessOverlayActive) {
      waitForRelease(BTN_YELLOW);
      app.mode3FileAccessOverlayActive = false;
      drawMode3Status();
    } else if (!app.mode3HelpOverlayActive) {
      showFileAccessScreen();
    }
  }
  // Same 5-minute generous-but-not-forever bound the old blocking screens
  // used, now polled instead of waited for.
  const unsigned long OVERLAY_TIMEOUT_MS = 5UL * 60UL * 1000UL;
  if (app.mode3HelpOverlayActive && millis() - app.mode3HelpOverlayShownMs > OVERLAY_TIMEOUT_MS) {
    Serial.println("Mode 3: help overlay timed out, returning to status screen");
    app.mode3HelpOverlayActive = false;
    drawMode3Status();
  }
  if (app.mode3FileAccessOverlayActive && millis() - app.mode3FileAccessOverlayShownMs > OVERLAY_TIMEOUT_MS) {
    Serial.println("Mode 3: file-access overlay timed out, returning to status screen");
    app.mode3FileAccessOverlayActive = false;
    drawMode3Status();
  }

  checkMode3CongestionScan();
  checkMode3HeapHealth();

  if (mode3NeedsNtpSync) {
    mode3NeedsNtpSync = false;
    syncRtcFromNtp();
  }

  if (mode3NeedsWebServerStart) {
    mode3NeedsWebServerStart = false;
    if (!fileServerStarted) {
      fileServer.begin();
      fileServerStarted = true;
      Serial.println("Mode 3: file-access web server started");
    }
  }

  // Build 7.5: drain every queued transition in order, instead of only
  // ever reacting to "what is app.mode3State right now" -- see the
  // Mode3EventEntry comment near its declaration for the problem this
  // fixes. A while loop rather than a single check: a brief AP blip can
  // queue up a DISCONNECTED and a CONNECTED (or even more) between one
  // mode3Loop() iteration and the next, and every one of them gets its
  // own logged row and its own contribution to the drop count/connected-
  // time bookkeeping now, not just whichever state happened to be current
  // when this was last checked.
  Mode3EventEntry entry;
  while (popMode3Event(entry)) {
    if (entry.type == MODE3_EVT_DISCONNECTED) {
      // The time since the last transition was connected time, so it
      // counts toward the running-summary uptime total, and this is a
      // real drop for the drop counter. Build 7 item 3: the driver's
      // disconnect reason goes into the row alongside the existing
      // interference/congestion note.
      mode3ConnectedAccumMs += entry.atMs - mode3LastStateChangeMs;
      mode3DropCount++;
      // NOTE: cast to int before String += -- Arduino's String class
      // treats a bare uint8_t/unsigned char argument to += as a
      // character to append, not a number to format (a well-known
      // Arduino-core gotcha), so an uncast reason code here would
      // silently append a control character instead of e.g. "2".
      String note = "reason=";
      note += (int)entry.disconnectReason;
      note += " (";
      note += entry.hadReason ? wifiDisconnectReasonToString(entry.disconnectReason) : "unknown";
      note += ")";
      String interferenceNote = buildInterferenceNote(mode3LastKnownRssi, mode3NearbyApCount);
      if (interferenceNote.length() > 0) note += " " + interferenceNote;
      // Build 7.5 item 2: a channel survey takes the radio off the
      // connected channel for a moment -- flag when a disconnect
      // coincided with one, so the log doesn't quietly blame the AP for
      // something this device's own scan may have caused.
      if (entry.duringChannelSurvey) {
        note += " (coincided with a channel-survey scan -- may be the scan itself briefly leaving the channel, not a genuine AP-side drop)";
      }
      appendMonitorLogLine("DISCONNECTED", RSSI_NOT_FOUND, note);
      if (mode3AlertPiezoEnabled) {
        mode3AlertActive = true;   // silenced by any button press, or by reconnecting (below)
      }
    } else {   // MODE3_EVT_CONNECTED
      // Just connected -- either for the first time this session, or a
      // reconnect after a drop (entry.wasReconnect, captured by
      // SysProvEvent() from app.mode3State at the moment this happened).
      // Either way, no need to keep demanding attention with the alert.
      mode3AlertActive = false;
      noTone(PIEZO);
      const char *evt = entry.wasReconnect ? "RECONNECTED" : "CONNECTED";
      // Build 7 item 4: the currently-connected AP's SSID/BSSID, logged
      // once per connection (here) rather than on every SAMPLE row. Read
      // at drain time (same as before this build) -- usually drained
      // within the same loop() iteration it was queued in, so this still
      // reflects the connection the entry is actually about.
      String note = "SSID=" + WiFi.SSID() + " BSSID=" + WiFi.BSSIDstr();
      if (entry.duringChannelSurvey) {
        note += " (channel-survey scan was in flight when this reconnected)";
      }
      appendMonitorLogLine(evt, WiFi.RSSI(), note);
    }
    mode3LastStateChangeMs = entry.atMs;
    refreshMode3Screen();
  }

  // --- Build 7 item 1: a disconnect that hasn't resolved within the grace
  // period is a genuine sustained loss, not a brief blip -- demote to
  // MODE3_OFFLINE (auto-reconnect keeps trying regardless) and log it as
  // its own distinct event, separate from the DISCONNECTED row already
  // logged the moment it happened. ---
  if (app.mode3State == MODE3_RECONNECTING && millis() - mode3ReconnectingSinceMs >= MODE3_RECONNECT_GRACE_MS) {
    app.mode3State = MODE3_OFFLINE;
    Serial.println("Mode 3: no reconnect within the grace period -- sustained loss");
    appendMonitorLogLine("CONNECTION_LOST", RSSI_NOT_FOUND,
                          "No reconnect within " + String(MODE3_RECONNECT_GRACE_MS / 1000) + "s of the disconnect -- treating as a sustained loss, not a brief drop.");
    refreshMode3Screen();
  }

  // --- Occasional channel-congestion check while connected (async, see
  // checkMode3CongestionScan() above) ---
  if (app.mode3State == MODE3_CONNECTED && !mode3ScanPending &&
      millis() - mode3LastCongestionCheckMs >= MODE3_CONGESTION_CHECK_INTERVAL_MS) {
    WiFi.scanNetworks(true);
    mode3ScanPending = true;
    mode3LastCongestionCheckMs = millis();
  }

  // --- Periodic RSSI sample while connected ---
  if (app.mode3State == MODE3_CONNECTED && millis() - mode3LastSampleMs >= MODE3_SAMPLE_INTERVAL_MS) {
    int rssi = WiFi.RSSI();
    mode3LastKnownRssi = rssi;     // used by buildInterferenceNote() if we drop before the next sample
    mode3RssiSum += rssi;
    mode3RssiSampleCount++;
    String recommendation = buildRecommendation(false, rssi, mode3NearbyApCount, mode3CongestionChannel, mode3RecommendedChannel);
    appendMonitorLogLine("SAMPLE", rssi, recommendation);
    Serial.print("Mode 3: sample RSSI ");
    Serial.print(rssi);
    Serial.println(" dBm");
    mode3LastSampleMs = millis();
  }

  // --- Disconnect alert: keep beeping until silenced (any button, or a
  // reconnect -- see above) ---
  if (mode3AlertActive) {
    updateMode3Alert();
  }

  // --- Red button: toggle the disconnect-alert piezo on/off ---
  if (digitalRead(BTN_RED) == LOW) {
    mode3AlertPiezoEnabled = !mode3AlertPiezoEnabled;
    Serial.println(mode3AlertPiezoEnabled ? "Mode 3: disconnect alert piezo enabled" : "Mode 3: disconnect alert piezo disabled");
    if (!mode3AlertPiezoEnabled) {
      mode3AlertActive = false;
      noTone(PIEZO);
    }
    refreshMode3Screen();
    waitForRelease(BTN_RED);
  }

  // --- Blue button (held): forget the stored network and restart into a
  // fresh SoftAP Prov setup, so a different network can be entered. Works
  // at any point while in Mode 3 — connected, still waiting for setup,
  // whatever — not just right after a fresh power-up.
  if (digitalRead(BTN_BLUE) == LOW) {
    if (blueHoldStartMs == 0) {
      blueHoldStartMs = millis();
    } else if (millis() - blueHoldStartMs >= BLUE_FORGET_HOLD_MS) {
      forgetMode3NetworkAndRestart();   // never returns — restarts the device
    }
  } else {
    blueHoldStartMs = 0;
  }

  // --- Green button: back to the mode menu ---
  if (digitalRead(BTN_GREEN) == LOW) {
    waitForRelease(BTN_GREEN);
    Serial.println("Returning to mode menu...");
    // Leaving Mode 3 cleanly disconnects rather than staying associated
    // in the background — Modes 1/2 are scan-only by design, and this
    // keeps behavior predictable if you switch back and forth.
    WiFi.disconnect();
    mode3Started = false;
    app.mode3State = MODE3_OFFLINE;
    app.mode3HelpOverlayActive = false;
    app.mode3FileAccessOverlayActive = false;
    enterMode(selectMode());
  }
}

// ===========================================================================
// Top-level dispatcher
// ===========================================================================

void loop() {
  switch (app.currentMode) {
    case 1: mode1Loop(); break;
    case 2: mode2Loop(); break;
    case 3: mode3Loop(); break;
  }
}
