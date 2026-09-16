# FilaMan Labels on SpoolmanScale Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a scale with a resolved FilaMan spool choose one of that user's saved label presets, request a PC print window with its Print and Export PDF actions, or print a monochrome label directly on a Phomemo M220 over BLE.

**Architecture:** FilaMan owns label rendering and PC requests. The scale uses its existing FilaMan user API key, keeps the selected preset ID and measured printer width in NVS, validates a packed monochrome raster before sending it to a separate M220 driver, and never puts the API key in the scale web page. The touchscreen offers the action for the currently resolved spool. Implement PC printing first so it is useful before M220 hardware calibration.

**Tech Stack:** PlatformIO `wt32-sc01-plus`, Arduino ESP32, existing `HTTPClient`/ArduinoJson/LVGL/Preferences, and the ESP32 Arduino BLE library already shipped with the platform. No PNG decoder or new package.

**Spec:** `../scale-label-api/docs/scale-label-api.md` in this workspace, from FilaMan branch `feat/scale-label-api` at `f9e6295a`. The required wire contract is repeated below so this plan remains usable if that worktree moves.

## Global constraints and wire contract

- Authenticate all three FilaMan calls with the existing **user** key: `Authorization: ApiKey <key>`, with `spools:read`. A device token cannot list user presets or request PC printing. Never log or send the key to the scale browser.
- `GET /api/v1/labels/presets` returns `[{"id":7,"name":"40 mm spool"}]`. ID `0` locally means the default FilaMan label and is sent by **omitting** `preset_id`.
- `POST /api/v1/labels/spool/{id}/print-request[?preset_id=7]` returns HTTP `201` and `{"id":42,"spool_id":123,"preset_id":7}`. The signed-in FilaMan tab for the same user prompts for five minutes; its label page offers **Print** and **Export PDF**. The scale displays **Request queued**, never **Printed**.
- `GET /api/v1/labels/spool/{id}/render?format=mono1&width={pixels}[&preset_id=7]` returns raw packed rows, top first, MSB first, `1` black, low padding bits zero. Check `X-Image-Width`, `X-Image-Height`, `X-Row-Bytes`, `X-Bit-Order: msb-black-1`, and the exact body length. Width is 384–1024 pixels; current server default is 576, which is only an initial scale setting.
- The scale requests monochrome only. Do not add a `format=png` or `color=color` call, PNG decoding, a scale color toggle, or a scale web preview in this release.
- Treat 401 as a key problem, 403 as key scope/user type, 404 as a stale spool or preset, and 422 as an invalid render parameter. Refresh presets after 404. Do not automatically repeat a `print-request` POST after an ambiguous timeout, since it could create a second prompt.
- The server renderer does not reproduce every advanced browser designer feature, including inline markup. Compare an important preset with the rendered output before printing it routinely.
- Keep printer protocol separate from FilaMan HTTP. The [myphomemo M220 entry](https://github.com/DeepCoreSystem/myphomemo#supported-printers) lists 648 pixels, but the actual usable width depends on the printer and media; measure it. Its [printer.js](https://github.com/DeepCoreSystem/myphomemo/blob/master/src/web/printer.js) routes M220 through the general M-series BLE raster path, while the M110 speed/media/footer sequence is selected separately. Its [BLE constants](https://github.com/DeepCoreSystem/myphomemo/blob/master/src/web/constants.js) use service `0xff00`, write `0xff02`, and notify `0xff03`. Reimplement only the needed bytes and transport; check source licensing before copying code.

## Existing code paths and intended files

| File | Change |
| --- | --- |
| `src/services/filaman_api.h/.cpp` | Add bounded preset-list and PC-request calls using existing `addApiKey()` and HTTP status style. |
| `src/services/label_raster.h` | Small Arduino-independent raster dimensions/body validator. |
| `src/services/filaman_labels.h/.cpp` | Fetch `mono1`, validate headers and exact bytes, own/free the PSRAM buffer. |
| `src/services/phomemo_m220_protocol.h` | Arduino-independent raster command bytes for host checking. |
| `src/services/phomemo_m220.h/.cpp` | BLE scan/connect/write and M220 command sequence; consume raster only. |
| `src/ui/more_info_screen.cpp`, `src/ui/label_print_screen.h/.cpp` | FilaMan-only Print label entry and dedicated preset/output/status screen. |
| `src/ui/navigation.cpp`, `src/app/app_loop.cpp` | Close the new overlay safely and run deferred HTTP/BLE operations outside LVGL callbacks. |
| `src/lang.h/.cpp` | German and English copy for the new controls, progress and errors. |
| `src/services/prefs_store.h` consumers | Store `label_preset` and `m220_width` with existing helpers; both NVS names are under 15 characters. |
| `test/label_protocol_selftest.cpp` | One host runnable check for raster bounds, row packing assumptions and M220 raster header bytes. |

Keep the scale web backend page as the existing place to enter the FilaMan user API key. Update its help text to say `spools:read` and same-user PC login if the current copy is unclear. The Print label action is on the touchscreen; no new scale web routes are required.

## Phase A: presets and PC print request

### Task 1: Fetch and select saved user presets

**Files:** `src/services/filaman_api.h/.cpp`, `src/ui/label_print_screen.h/.cpp`, `src/ui/more_info_screen.cpp`, `src/ui/navigation.cpp`, `src/app/app_loop.cpp`, `src/lang.h/.cpp`.

**Interface:** Add `struct FilaManLabelPreset { int id; char name[64]; };` and `int filamanListLabelPresets(const char* base_url, const char* api_key, FilaManLabelPreset* out, size_t capacity, size_t* count, uint32_t timeout_ms = 8000);`. Return HTTP status or a negative local error; set `*count=0` on failure. A list exceeding capacity is an explicit error, never a silently truncated list. Use a capacity of 64 and a bounded response/parse size. Store the selected ID with `prefsPutInt("label_preset", id)`; `0` is the default label.

- [ ] Add a FilaMan API client request using `addApiKey()`, `HTTPClient::GET()`, and ArduinoJson. Reject non-array JSON, missing/negative IDs, names that cannot fit, and more than 64 entries. Keep the HTTP result for the UI to classify.
- [ ] Add a FilaMan-only **Print label** button in the free right side of the More Info header when `sm_found && sm_id > 0`. The LVGL callback captures `sm_id` into the new screen state and sets a deferred open flag; it does no network work.
- [ ] Build a scrollable preset selector headed by **Default label** plus returned names. When opened, fetch the list once; provide a Refresh control. If the saved ID no longer exists, select Default and show a short notice. Keep the selected ID when leaving the screen.
- [ ] Add new German/English `STR_` entries and ensure `hideAllOverlays()`/`showMainScreen()` close the screen and clear any pointers before a later async result can write to them.
- [ ] Run `PLATFORMIO_CORE_DIR=/tmp/filaman-scale-platformio-core PLATFORMIO_SETTING_ENABLE_TELEMETRY=no pio run -e wt32-sc01-plus`; inspect the new screen on the device with empty list, several names, long names and no WiFi. Commit this independently usable preset picker.

### Task 2: Ask an open FilaMan PC tab to show the print page

**Files:** `src/services/filaman_api.h/.cpp`, `src/ui/label_print_screen.cpp`, `src/web/pages/page_backend.cpp`, `src/lang.h/.cpp`.

**Interface:** Add `int filamanRequestLabelPrint(const char* base_url, const char* api_key, int spool_id, int preset_id, int* request_id, uint32_t timeout_ms = 8000);`. For `preset_id == 0`, omit the query parameter. Return `201` only after parsing a positive request ID and matching spool ID; return a negative parse error for malformed success JSON.

- [ ] Add the POST using the existing FilaMan user-key helper. Do not use `filamanDeviceToken()` and do not retry a timed-out POST automatically.
- [ ] Add **Open on PC** to the label screen. Freeze spool/preset IDs on tap, show a pending state immediately, then call the API from the app loop's deferred-action section under `HttpStallTime`, not inside the LVGL event callback. Ignore a second tap while pending.
- [ ] On `201`, show **Request queued — approve it in the FilaMan tab. Choose Print or Export PDF there.** On 401/403/404/422, show the concrete recovery from Global constraints. A queued request is never shown as printer success.
- [ ] Clarify the existing backend key help copy: user API key, `spools:read`, and the same signed-in FilaMan account on the PC. The scale web form continues to store the key server-side only.
- [ ] Build firmware, then test against a running FilaMan server: same-user tab receives one prompt and opens the page; Export PDF works; a different user's tab sees nothing; a rapid double tap creates one request; an unreachable server shows a failure without falsely saying queued. Commit the PC path.

## Phase B: monochrome BLE printing

### Task 3: Fetch a complete, validated 1-bit image

**Files:** `src/services/label_raster.h`, `src/services/filaman_labels.h/.cpp`, `test/label_protocol_selftest.cpp`.

**Interface:** Define `struct LabelRaster { uint16_t width, height, row_bytes; uint8_t* pixels; size_t length; };`, `bool labelRasterShapeValid(uint16_t width, uint16_t height, uint16_t row_bytes, size_t length);`, `bool labelRasterPaddingValid(const LabelRaster& image);`, `int filamanFetchMonoLabel(const char* base_url, const char* api_key, int spool_id, int preset_id, uint16_t requested_width, LabelRaster* out, uint32_t timeout_ms = 12000);`, and `void filamanFreeLabel(LabelRaster* image);`. `out` starts empty and remains empty on any failure.

- [ ] Write a host self-check with `assert(labelRasterShapeValid(480,320,60,19200))`, `assert(!labelRasterShapeValid(480,320,60,19199))`, `assert(!labelRasterShapeValid(480,320,61,19520))`, `assert(!labelRasterShapeValid(0,320,0,0))`, and a 385-pixel row of 49 bytes whose last byte is `0x80` (valid) versus `0x81` (invalid padding). Run with `c++ -std=c++17 -Isrc test/label_protocol_selftest.cpp -o /tmp/label_protocol_selftest && /tmp/label_protocol_selftest`; confirm compilation fails before the validator exists.
- [ ] Implement the validator with `row_bytes == (width + 7) / 8`, `length == row_bytes * height`, requested width 384–1024, and a conservative 256 KiB image ceiling. Validate `X-Bit-Order` exactly and numeric headers strictly; reject negative, missing and nonnumeric values. `labelRasterPaddingValid()` checks unused low bits in the last byte of each row when width is not divisible by eight.
- [ ] Use `HTTPClient` with `collectHeaders()` before GET, stream exactly the validated byte count into a PSRAM allocation, and detect a short or extra body before returning success. Always call `http.end()` and free the allocation on failure. Never deserialize binary data as JSON or store it in a `String`.
- [ ] Run the host self-check and PlatformIO build. Against FilaMan, fetch a default and saved preset at 576 pixels; verify returned dimensions and byte count. Test malformed headers/body with a local fake HTTP response or a controlled device endpoint, and confirm no printing begins. Commit the validated fetcher.

### Task 4: Connect to and print on one M220

**Files:** `src/services/phomemo_m220_protocol.h`, `src/services/phomemo_m220.h/.cpp`, `test/label_protocol_selftest.cpp`, `src/ui/label_print_screen.cpp`, `src/lang.h/.cpp`.

**Interface:** `bool phomemoM220Print(const char* ble_address, const LabelRaster& image, char* error, size_t error_size);` consumes but does not free the validated image. Keep BLE device selection as one stored address (`"m220_addr"`); a Scan button lists discovered M220 names/addresses and allows changing the selection. Do not build a generic printer registry for one printer.

- [ ] Extend the host self-check with `const std::array<uint8_t,8> expected = {0x1d,0x76,0x30,0x00,0x48,0x00,0x90,0x01}; assert(m220RasterHeader(576,400) == expected);`. Define this Arduino-independent function in `phomemo_m220_protocol.h`; confirm compilation fails before adding it.
- [ ] Implement the small M220 command builder and BLE writer using the platform's existing BLE library. Start from myphomemo's M-series `ESC @`, heat/density, GS `v 0`, raster, and feed sequence; use service `0xff00` and writable `0xff02`. Begin at 128-byte chunks with 20 ms pacing, but respect the negotiated characteristic write size. Report connection, missing characteristic, write and disconnect failures. Avoid M110-only speed/media/footer bytes unless real M220 testing proves they are needed.
- [ ] Add scan/select, printable-width adjustment from 384 to 1024 pixels in 8-pixel steps (initial value 576), and **Print on M220**. Persist width as `"m220_width"`; send exactly that width to FilaMan. Freeze spool, preset, width and printer address on tap; fetch the raster first, then print, then free it on all paths. Show **Sent to printer** only when writes finish; this is not proof the paper printed.
- [ ] Build firmware and run the self-check. On actual M220/media, print a border/step calibration image at several widths to find the largest reliable width, correct orientation, and feed amount. Save that measured width in the UI; compare a real FilaMan preset against its monochrome render. Confirm a failed or interrupted download never sends printer bytes, and a BLE disconnect is reported. Commit the hardware path.

### Task 5: End-to-end cleanup and release check

**Files:** `src/ui/label_print_screen.cpp`, `src/ui/navigation.cpp`, `src/app/app_loop.cpp`, `src/lang.h/.cpp`, this plan if actual hardware changes the command sequence.

- [ ] Exercise navigation during preset loading, during a PC request, and during BLE transfer. Guard every UI update against a closed screen and recheck the captured spool ID only to label the result, never to silently switch the in-flight request to a newly scanned tag.
- [ ] Exercise 401, 403, deleted preset 404, 422 invalid width, no WiFi, no PSRAM, no printer, low printer battery/paper out if notifications support it, and repeated taps. Ensure messages give an actionable next step and no key or raster content reaches logs.
- [ ] Run the host self-check and a clean `pio run -e wt32-sc01-plus`. Record flash/RAM use against the baseline (flash 68.7%, RAM 58.6%) and test WiFi and BLE coexistence on hardware. Recheck that existing Spoolman and BamBuddy flows show no label button.
- [ ] Confirm the signed-in PC flow still offers **Print** and **Export PDF** and that the scale only says **Request queued**. Compare at least one preset with the FilaMan browser output; document any advanced-layout difference. Commit final corrections.

## Delivery boundary

Phase A is a useful first deliverable without a printer: saved preset selection and a PC prompt whose page can print or export PDF. Phase B adds monochrome M220 printing once a real device establishes the usable width and protocol behavior. Color PNG and scale web preview are intentionally absent per the current product decision.
