# Modular Label Printers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make label printing model-aware, retain the verified M220 path, add experimental M110 support, list all useful BLE scan results, and show visible busy feedback throughout the label workflow.

**Architecture:** A small `label_printer` facade owns the selected model, BLE device, media dimensions, validation, and dispatch. A shared Phomemo M-series implementation owns BLE scanning and transport while explicit M220 and M110 command branches keep model behavior separate. Existing LVGL loading-overlay and HTTP-progress facilities provide busy feedback without adding another UI framework.

**Tech Stack:** ESP32-S3 Arduino framework, ESP32 BLE Arduino, LVGL 8.3, PlatformIO, NVS preferences, C++11 host self-tests, Python test harnesses.

**Spec:** `docs/superpowers/specs/2026-09-20-modular-label-printers-design.md`

## Global Constraints

- M220 remains the default model, but a fresh installation has no configured printer until a BLE address is selected.
- Bluetooth names are sorting hints only; no name may be filtered out or used to choose the protocol.
- The verified M220 init, density, raster, feed, 128-byte chunk limit, and 20 ms pacing remain byte-for-byte unchanged.
- M110 is marked experimental and uses a 384-dot, approximately 48 mm printable-width limit until hardware verification.
- M220 and M110 ship in the normal firmware; there is no driver installation flow or new dependency.
- Existing `m220_*` preferences remain readable, and clearing a migrated printer must not resurrect the legacy address.
- Spool loading, preset loading, BLE scanning, preview rendering, PC-print requests, and BLE printing all show a visible loading overlay that clears on success and every error path.
- The final binary must remain below the installed-device OTA limit of 3,145,728 bytes.
- German and English strings are required; French strings must remain present or fall back according to the existing language-table rules.

## Review Focus

- **Legacy settings:** an existing M220 address, name, and media size load as M220 until saved generically; a later clear stays cleared instead of falling back to legacy keys. Task 1 tests both paths.
- **Unreliable BLE names:** blank, unfamiliar, duplicate, lower-case, M110/M220, and `Q...` advertisements remain selectable; only ordering changes. Tasks 1 and 2 test these inputs.
- **Crowded BLE scans:** the saved device and likely Phomemo devices survive the fixed embedded result limit even when ordinary devices arrive first. Task 2 tests priority retention at capacity.
- **Busy-state cleanup:** WiFi, HTTP, allocation, invalid-raster, BLE-connect, and BLE-write failures all remove the loading overlay and permit another attempt. Tasks 3 and 4 test the owning UI paths.
- **Untested M110 hardware:** protocol bytes, raster width, media validation, orientation, and dispatch are pinned by host checks while the UI continues to label M110 experimental. Tasks 1, 2, and 5 cover this boundary.

---

### Task 1: Generic printer model, configuration, and ordering

**Files:**
- Create: `src/services/label_printer.h`
- Create: `src/services/label_printer.cpp`
- Create: `test/test_label_printer.py`

**Interfaces:**
- Consumes: `LabelRaster` from `src/services/label_raster.h`; existing `prefsGet*` and `prefsPut*` calls from `src/services/prefs_store.h`.
- Produces: `LabelPrinterModel`, `LabelPrinterProfile`, `LabelPrinterConfig`, `LabelPrinterDevice`, `LabelPrinterProgressFn`, `labelPrinterProfile()`, `labelPrinterLoadConfig()`, `labelPrinterSaveConfig()`, `labelPrinterConfigured()`, `labelPrinterRasterWidth()`, `labelPrinterRasterFits()`, `labelPrinterDevicePriority()`, `labelPrinterConsiderDevice()`, and `labelPrinterSortDevices()`.

- [ ] **Step 1: Write the failing configuration and ordering test**

Create `test/test_label_printer.py`. Compile `src/services/label_printer.cpp` with temporary stubs for Arduino strings and preferences. Its C++ test body must exercise these exact cases:

```cpp
int main() {
  clearPrefs();
  LabelPrinterConfig fresh = labelPrinterLoadConfig();
  assert(fresh.model == LabelPrinterModel::M220);
  assert(!labelPrinterConfigured(fresh));
  assert(fresh.media_width_mm == 40 && fresh.media_length_mm == 30);

  putLegacy("Q123456789", "7e:11:22:33:44:55", 40, 30);
  LabelPrinterConfig legacy = labelPrinterLoadConfig();
  assert(legacy.model == LabelPrinterModel::M220);
  assert(strcmp(legacy.address, "7e:11:22:33:44:55") == 0);

  legacy.address[0] = '\0';
  legacy.name[0] = '\0';
  assert(labelPrinterSaveConfig(legacy));
  assert(!labelPrinterConfigured(labelPrinterLoadConfig()));

  LabelPrinterConfig m110 = legacy;
  m110.model = LabelPrinterModel::M110;
  m110.media_width_mm = 50;
  m110.media_length_mm = 30;
  assert(labelPrinterSaveConfig(m110));
  LabelPrinterConfig normalized = labelPrinterLoadConfig();
  assert(normalized.model == LabelPrinterModel::M110);
  assert(normalized.media_width_mm == 40);  // invalid 50 mm printable width resets

  assert(labelPrinterRasterWidth(LabelPrinterModel::M220, 40) == 576);
  assert(labelPrinterRasterWidth(LabelPrinterModel::M110, 40) == 384);
  uint8_t pixels[48 * 240] = {};
  LabelRaster raster{384, 240, 48, pixels, sizeof(pixels), 320, false};
  assert(labelPrinterRasterFits(LabelPrinterModel::M110, raster, 40, 30));
  assert(!labelPrinterRasterFits(LabelPrinterModel::M110, raster, 48, 30));

  LabelPrinterConfig selected = normalized;
  strcpy(selected.address, "aa:aa:aa:aa:aa:aa");
  LabelPrinterDevice devices[] = {
    {{0}, "dd:dd:dd:dd:dd:dd"},
    {"speaker", "cc:cc:cc:cc:cc:cc"},
    {"Q123456789", "bb:bb:bb:bb:bb:bb"},
    {"m110-label", "ee:ee:ee:ee:ee:ee"},
    {"saved", "aa:aa:aa:aa:aa:aa"},
  };
  labelPrinterSortDevices(devices, 5, selected);
  assert(strcmp(devices[0].address, selected.address) == 0);
  assert(strcmp(devices[1].name, "m110-label") == 0);
  assert(strcmp(devices[2].name, "Q123456789") == 0);
  assert(devices[4].name[0] == '\0');

  LabelPrinterDevice kept[3] = {};
  size_t kept_count = 0;
  const LabelPrinterDevice candidates[] = {
    {"speaker", "01:01:01:01:01:01"},
    {"watch", "02:02:02:02:02:02"},
    {"keyboard", "03:03:03:03:03:03"},
    {"saved", "aa:aa:aa:aa:aa:aa"},
    {"Q123456789", "04:04:04:04:04:04"},
    {"Q123456789", "04:04:04:04:04:04"},
  };
  for (const auto& candidate : candidates)
    labelPrinterConsiderDevice(kept, &kept_count, 3, candidate, selected);
  labelPrinterSortDevices(kept, kept_count, selected);
  assert(kept_count == 3);
  assert(strcmp(kept[0].address, selected.address) == 0);
  assert(strcmp(kept[1].name, "Q123456789") == 0);
}
```

The Python harness must provide in-memory implementations of every `prefsGet*` and `prefsPut*` function used by the service, including `prefsGetBool("printer_mig", false)`.

- [ ] **Step 2: Run the test and verify the generic service is missing**

Run:

```bash
python3 test/test_label_printer.py
```

Expected: FAIL because `services/label_printer.h` and its functions do not exist.

- [ ] **Step 3: Define the public types and functions**

Create `src/services/label_printer.h` with this public surface:

```cpp
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "services/label_raster.h"

enum class LabelPrinterModel : uint8_t { NONE = 0, M220 = 1, M110 = 2 };

struct LabelPrinterProfile {
  LabelPrinterModel model;
  const char* name;
  uint16_t default_width_mm, default_length_mm;
  uint16_t min_width_mm, max_width_mm;
  uint16_t min_length_mm, max_length_mm;
  uint16_t base_raster_width, max_raster_width;
  bool experimental;
};

struct LabelPrinterConfig {
  LabelPrinterModel model;
  char name[32];
  char address[18];
  uint16_t media_width_mm, media_length_mm;
};

struct LabelPrinterDevice { char name[32]; char address[18]; };
using LabelPrinterProgressFn = void (*)();

const LabelPrinterProfile& labelPrinterProfile(LabelPrinterModel model);
LabelPrinterConfig labelPrinterLoadConfig();
bool labelPrinterSaveConfig(const LabelPrinterConfig& config);
bool labelPrinterConfigured(const LabelPrinterConfig& config);
uint16_t labelPrinterDotsForMm(uint16_t mm);
uint16_t labelPrinterRasterWidth(LabelPrinterModel model, uint16_t media_width_mm);
bool labelPrinterRasterFits(LabelPrinterModel model, const LabelRaster& image,
                            uint16_t media_width_mm, uint16_t media_length_mm);
uint8_t labelPrinterDevicePriority(const LabelPrinterDevice& device,
                                   const LabelPrinterConfig& selected);
void labelPrinterConsiderDevice(LabelPrinterDevice* devices, size_t* count,
                                size_t capacity,
                                const LabelPrinterDevice& candidate,
                                const LabelPrinterConfig& selected);
void labelPrinterSortDevices(LabelPrinterDevice* devices, size_t count,
                             const LabelPrinterConfig& selected);
```

Use these exact profiles in `src/services/label_printer.cpp`:

```cpp
static const LabelPrinterProfile kM220 = {
  LabelPrinterModel::M220, "M220", 40, 30, 20, 75, 10, 150, 576, 648, false
};
static const LabelPrinterProfile kM110 = {
  LabelPrinterModel::M110, "M110", 40, 30, 20, 48, 10, 150, 384, 384, true
};
```

Return an all-zero `NONE` profile for invalid model values. Use the existing 203 DPI rounding formula `(mm * 2030 + 127) / 254`. Raster width is the larger of the model's base width and the media dots, rounded up to a byte, but never above its maximum.

- [ ] **Step 4: Implement migration-safe configuration reads and writes**

Use generic NVS keys no longer than ESP32 NVS's 15-character limit:

```cpp
"printer_mig"   // bool: generic settings have been explicitly saved
"printer_model" // int
"printer_addr"  // string
"printer_name"  // string
"printer_w"     // int
"printer_h"     // int
```

When `printer_mig` is false, read `m220_addr`, `m220_name`, `m220_media_w`, and `m220_media_h` as an M220 configuration. When true, never consult legacy keys. Normalize an invalid model to M220 and invalid dimensions to that model's defaults. In `labelPrinterSaveConfig()`, validate and write all five generic values first, then write `printer_mig=true` last; return false if any write fails.

This ordering is required: an empty generic address saved after migration must remain empty even while old `m220_addr` still exists for firmware rollback.

- [ ] **Step 5: Implement pure raster validation and stable device ordering**

`labelPrinterRasterFits()` must require:

```cpp
image.width == labelPrinterRasterWidth(model, media_width_mm)
image.content_width == labelPrinterDotsForMm(media_width_mm)
image.height == labelPrinterDotsForMm(media_length_mm)
```

and reject invalid model/media ranges before those comparisons.

Implement stable insertion sorting with these priorities, lower first:

1. exact saved BLE address;
2. case-insensitive occurrence of the selected profile name (`M220` or `M110`);
3. a name of at least ten characters beginning with `Q` or `q`;
4. case-insensitive occurrence of `PHOMEMO`;
5. every other name, including an empty name.

Do not alphabetize within a priority; preserve scan order. `labelPrinterConsiderDevice()` deduplicates by address, appends while space remains, and at capacity replaces only the last device when the candidate has better priority. This makes the behavior deterministic without making the advertised name authoritative.

- [ ] **Step 6: Run the focused test**

Run:

```bash
python3 test/test_label_printer.py
```

Expected: PASS, including legacy clear behavior, M110 normalization, raster limits, blank names, and stable sorting.

- [ ] **Step 7: Commit the generic service**

```bash
git add src/services/label_printer.h src/services/label_printer.cpp test/test_label_printer.py
git commit -m "Add generic label printer service"
```

### Task 2: Shared Phomemo BLE transport and M110 protocol

**Files:**
- Create: `src/services/phomemo_m_series.h`
- Create by rename: `src/services/phomemo_m_series.cpp` from `src/services/phomemo_m220.cpp`
- Create by rename: `src/services/phomemo_m_series_protocol.h` from `src/services/phomemo_m220_protocol.h`
- Modify temporarily: `src/services/phomemo_m220.h` as a compatibility header for existing UI callers
- Modify: `src/services/label_printer.h`
- Modify: `src/services/label_printer.cpp`
- Modify: `test/label_protocol_selftest.cpp`
- Modify: `test/test_label_printer.py`

**Interfaces:**
- Consumes: `LabelPrinterModel`, `LabelPrinterDevice`, `LabelPrinterConfig`, and `LabelPrinterProgressFn` from Task 1.
- Produces: `phomemoMSeriesScan()` and `phomemoMSeriesPrint()` for the generic facade.

- [ ] **Step 1: Extend the protocol self-test before renaming implementation files**

Change `test/label_protocol_selftest.cpp` to include `services/label_printer.h` and the new `services/phomemo_m_series_protocol.h`. Preserve the existing raster and preview assertions, then add:

```cpp
const std::array<uint8_t, 8> m220_header =
    {0x1d, 0x76, 0x30, 0x00, 0x48, 0x00, 0x90, 0x01};
assert(phomemoRasterHeader(576, 400) == m220_header);
assert(phomemoWriteChunk(23) == 20);
assert(phomemoWriteChunk(247) == 128);

assert(m110SpeedCommand(5) ==
       (std::array<uint8_t, 4>{0x1b, 0x4e, 0x0d, 0x05}));
assert(m110DensityCommand(10) ==
       (std::array<uint8_t, 4>{0x1b, 0x4e, 0x04, 0x0a}));
assert(m110MediaCommand(0x0a) ==
       (std::array<uint8_t, 3>{0x1f, 0x11, 0x0a}));
assert(m110FooterStart() ==
       (std::array<uint8_t, 4>{0x1f, 0xf0, 0x05, 0x00}));
assert(m110FooterEnd() ==
       (std::array<uint8_t, 4>{0x1f, 0xf0, 0x03, 0x00}));
```

- [ ] **Step 2: Run the self-test and verify the M-series protocol header is missing**

Run:

```bash
g++ -std=c++11 -Isrc test/label_protocol_selftest.cpp -o /tmp/label_protocol_selftest
```

Expected: FAIL because `phomemo_m_series_protocol.h` and M110 command builders do not exist.

- [ ] **Step 3: Rename the M220 files and expose the shared transport**

Run:

```bash
git mv src/services/phomemo_m220.cpp src/services/phomemo_m_series.cpp
git mv src/services/phomemo_m220_protocol.h src/services/phomemo_m_series_protocol.h
```

Create `src/services/phomemo_m_series.h` with:

```cpp
#pragma once
#include "services/label_printer.h"

size_t phomemoMSeriesScan(LabelPrinterDevice* out, size_t capacity,
                          const LabelPrinterConfig& selected,
                          LabelPrinterProgressFn progress = nullptr);
bool phomemoMSeriesPrint(LabelPrinterModel model, const char* ble_address,
                         const LabelRaster& image, char* error,
                         size_t error_size,
                         LabelPrinterProgressFn progress = nullptr);
```

Keep `src/services/phomemo_m220.h` for this task only so the still-unmodified UI builds between commits. Make `M220Device` an alias of `LabelPrinterDevice`, and declare the old two functions as wrappers implemented at the bottom of `phomemo_m_series.cpp`. The scan wrapper loads the current configuration and delegates to the M-series scan; the print wrapper dispatches `LabelPrinterModel::M220`. Task 4 removes this compatibility surface after the last UI caller moves.

Add `labelPrinterScan()` and `labelPrinterPrint()` to `label_printer.h` with the signatures below, then implement them as the only production facade over the M-series calls:

```cpp
size_t labelPrinterScan(const LabelPrinterConfig& selected,
                        LabelPrinterDevice* out, size_t capacity,
                        LabelPrinterProgressFn progress = nullptr);
bool labelPrinterPrint(const LabelPrinterConfig& config, const LabelRaster& image,
                       char* error, size_t error_size,
                       LabelPrinterProgressFn progress = nullptr);
```

`labelPrinterPrint()` rejects missing model/address, invalid raster padding, model-width overflow, and loaded-media mismatch before opening BLE.

- [ ] **Step 4: Preserve the proven M220 sequence and add explicit M110 commands**

In `phomemo_m_series_protocol.h`, rename the common helpers to `phomemoRasterHeader()` and `phomemoWriteChunk()`. Add the five M110 builders asserted in Step 1.

In `phomemo_m_series.cpp`, keep the current BLE service `0xff00`, write characteristic `0xff02`, GATT callback queues, connection cleanup, chunk calculation, response-mode selection, and 20 ms delay. Keep the M220 send branch exactly:

```text
init     1b 40
density  1b 37 07 64 64
raster   1d 76 30 00 + width bytes + height + bitmap
feed     1b 4a 20
```

Add a separate M110 branch:

```text
speed        1b 4e 0d 05
density      1b 4e 04 0a
gap media    1f 11 0a
raster       1d 76 30 00 + width bytes + height + bitmap
footer start 1f f0 05 00
footer end   1f f0 03 00
```

Invoke `progress()` after each successfully sent chunk and after each scan interval when it is non-null. Error messages use the profile name supplied by `labelPrinterProfile(model)` rather than literal `M220`, while serial stages retain the model name for diagnostics.

- [ ] **Step 5: Scan every advertisement and retain priority devices at capacity**

Remove `M220ScanCollector`'s name filter. Deduplicate by BLE address and retain up to `capacity` devices. Scan in eight one-second intervals so `progress()` advances even when no advertisement arrives.

Pass every advertisement to `labelPrinterConsiderDevice()`, then sort once at the facade boundary. Empty names remain empty in the service; the UI supplies their display fallback.

Keep the over-capacity and duplicate-address assertions added in Task 1.

- [ ] **Step 6: Make the old test names disappear**

Run:

```bash
rg -n "phomemoM220|M220Device|m220RasterHeader|m220WriteChunk" src test tests
```

Expected at this task boundary: old names occur only in the temporary compatibility header/wrappers and existing UI callers. No old protocol helper remains in `test/label_protocol_selftest.cpp`.

- [ ] **Step 7: Run the host protocol and service tests**

Update the Python harness so its generated C++ source supplies link stubs with the real signatures:

```cpp
size_t phomemoMSeriesScan(LabelPrinterDevice*, size_t,
                          const LabelPrinterConfig&, LabelPrinterProgressFn) {
  return 0;
}
bool phomemoMSeriesPrint(LabelPrinterModel, const char*, const LabelRaster&,
                         char*, size_t, LabelPrinterProgressFn) {
  return false;
}
```

Run:

```bash
python3 test/test_label_printer.py
g++ -std=c++11 -Isrc test/label_protocol_selftest.cpp -o /tmp/label_protocol_selftest
/tmp/label_protocol_selftest
```

Expected: both commands PASS.

- [ ] **Step 8: Commit the shared transport and protocol**

```bash
git add src/services/label_printer.cpp src/services/phomemo_m_series.h \
  src/services/phomemo_m_series.cpp src/services/phomemo_m_series_protocol.h \
  src/services/phomemo_m220.h src/services/label_printer.h \
  test/label_protocol_selftest.cpp test/test_label_printer.py
git commit -m "Add modular Phomemo M-series drivers"
```

### Task 3: Model-aware printer settings and full BLE device list

**Files:**
- Modify: `src/ui/printer_settings_screen.cpp`
- Modify: `src/lang.h`
- Modify: `src/lang.cpp`
- Create: `test/test_printer_settings.py`

**Interfaces:**
- Consumes: all Task 1 facade functions; `loadingOverlayShow()`, `loadingOverlayTick()`, and `loadingOverlayHide()` from the existing loading overlay.
- Produces: a touchscreen configuration containing an independently selected model and BLE address, plus model-aware label-size controls.

- [ ] **Step 1: Write a host UI regression harness**

Create `test/test_printer_settings.py` using the same temporary-header technique as `test/test_filaman_print_deferred.py`. Stub the generic printer service and loading overlay, compile the production `printer_settings_screen.cpp`, and drive its deferred callbacks. The LVGL stub records every label string in `std::vector<std::string> label_text`; `renderedText()` searches that vector. `tapModelButton()` invokes the callback on the 112 x 44 model button, and `selectModel()` invokes the model-row callback whose user data matches the requested enum.

The harness must assert:

```cpp
requestPrinterSettingsScreen();
handlePrinterSettingsDeferredActions();
assert(renderedText("M220"));

tapModelButton();
handlePrinterSettingsDeferredActions();
selectModel(LabelPrinterModel::M110);
handlePrinterSettingsDeferredActions();
assert(saved.model == LabelPrinterModel::M110);
assert(renderedText("Experimental"));

tapScanButton();
handlePrinterSettingsDeferredActions();
assert(scan_calls == 1);
assert(loading_shown == 1 && loading_hidden == 1);
assert(renderedText("Q123456789"));
assert(renderedText("Unknown BLE device"));

scan_result = 0;
tapScanButton();
handlePrinterSettingsDeferredActions();
assert(loading_shown == 2 && loading_hidden == 2);
assert(renderedText("No Bluetooth devices found"));
```

Also select a 50 mm M220 size, switch to M110, and assert the saved width becomes the M110 default 40 mm because 50 exceeds its printable range.

- [ ] **Step 2: Run the UI harness and verify it fails on the M220-only screen**

Run:

```bash
python3 test/test_printer_settings.py
```

Expected: FAIL because the screen directly reads `m220_*`, has no model page, filters through the old M220 scan, and does not use the loading overlay.

- [ ] **Step 3: Replace direct preferences with one loaded configuration**

Include `services/label_printer.h` and remove `services/phomemo_m220.h` plus all direct `m220_*` reads and writes. Keep a screen-local `LabelPrinterConfig config`, refreshed by `labelPrinterLoadConfig()` when the main page opens.

Every model, device, clear, or media selection modifies a copy and calls `labelPrinterSaveConfig(config)`. If saving fails, show the existing settings error style and retain the on-screen previous value.

- [ ] **Step 4: Add the model selector without shrinking the established controls**

Add `Page::MODELS`. Keep the main screen's existing selected-printer card height and place a 112 x 44 outline model button on its right side. The left side continues to show the selected BLE name/address. The model button displays `M220` or `M110` and opens a dedicated model list with two large rows.

The M110 row includes the translated `Experimental` caption. Selecting a model keeps the BLE address, resets media to the new model defaults only if the current dimensions fall outside its profile, saves, and returns to the main printer page.

- [ ] **Step 5: Render every returned BLE device and generic copy**

Use a 24-entry static `LabelPrinterDevice` array. Call:

```cpp
device_count = labelPrinterScan(config, devices, 24, loadingOverlayTick);
```

Use the device name when non-empty and translated `Unknown BLE device` otherwise; always show the address. The service has already sorted saved/model/`Q...` hints first. Selecting a device changes only `config.name` and `config.address`, never `config.model`.

Replace M220-specific visible copy with generic strings:

- `Scan Bluetooth`
- `No Bluetooth devices found`
- `Select a Bluetooth device`
- `Printer model`
- `Experimental`
- `Unknown BLE device`

Add complete German, English, and French cells in `lang.cpp` and matching enum entries in `lang.h`.

- [ ] **Step 6: Make media choices use the selected profile**

Replace fixed 20-75 and 10-150 bounds in saved values, custom adjustments, range captions, and common-size filtering with `labelPrinterProfile(config.model)`.

Hide common size rows outside the active profile. For M110, do not offer widths above 48 mm. Keep all existing M220 sizes and behavior.

- [ ] **Step 7: Show and clear the scan overlay on every outcome**

Wrap the deferred scan action exactly once:

```cpp
loadingOverlayShow(T(STR_PRINTER_SCANNING));
device_count = labelPrinterScan(config, devices, 24, loadingOverlayTick);
loadingOverlayHide();
renderDevices();
```

The overlay is created before BLE starts and removed before rendering success or empty state. The full-screen overlay already swallows touches, so a second scan cannot be queued while the first runs.

- [ ] **Step 8: Run the printer settings and language checks**

Run:

```bash
python3 test/test_printer_settings.py
python3 scripts/check_conventions.py --selftest
scripts/check.sh
```

Expected: PASS. `scripts/check.sh` may report its existing build-dependent warning if the firmware has not yet been rebuilt, but it must report no language-table or convention failure.

- [ ] **Step 9: Commit the settings UI**

```bash
git add src/ui/printer_settings_screen.cpp src/lang.h src/lang.cpp \
  test/test_printer_settings.py
git commit -m "Add printer model and BLE device selection"
```

### Task 4: Route label printing through the facade and add busy feedback

**Files:**
- Modify: `src/ui/label_print_screen.cpp`
- Modify: `src/services/filaman_labels.cpp`
- Modify: `src/ui/header_status.cpp`
- Modify: `src/ui/connection_screen.cpp`
- Modify: `src/ui/settings_screen.cpp`
- Modify: `src/ui/manual_spool_screen.cpp`
- Modify: `src/services/phomemo_m_series.cpp`
- Delete: `src/services/phomemo_m220.h`
- Modify: `src/lang.h`
- Modify: `src/lang.cpp`
- Modify: `test/test_filaman_print_deferred.py`
- Modify: `test/test_filaman_print_flow.py`
- Create: `test/test_manual_spool_loading.py`

**Interfaces:**
- Consumes: `labelPrinterLoadConfig()`, `labelPrinterConfigured()`, `labelPrinterRasterWidth()`, `labelPrinterRasterFits()`, and `labelPrinterPrint()` from Tasks 1-2.
- Produces: model-neutral status/header/navigation behavior and visible busy states for every blocking label action.

- [ ] **Step 1: Update the deferred print harness before production code**

Replace the M220 stubs in `test/test_filaman_print_deferred.py` with the generic service surface and loading-overlay counters. Add assertions for these flows:

```cpp
// Successful preview fetch.
requestLabelPreviewScreen(123);
handleLabelPrintDeferredActions();
assert(loading_depth == 0 && loading_shown == loading_hidden);

// Preview allocation failure.
fetch_response = FILAMAN_LABEL_NO_PSRAM;
requestLabelPreviewScreen(123);
handleLabelPrintDeferredActions();
assert(loading_depth == 0);

// Preset HTTP failure.
preset_response = 500;
requestLabelPresetSettingsScreen();
handleLabelPrintDeferredActions();
assert(loading_depth == 0);

// BLE connection/write failure.
print_result = false;
tap(255, 261);
handleLabelPrintDeferredActions();
assert(print_calls == 1 && loading_depth == 0);

// PC request failure.
response = 500;
tap(45, 261);
handleLabelPrintDeferredActions();
assert(loading_depth == 0);
```

The generic config stub must cover M220 and M110, including an empty address that produces the select-device status without showing a print overlay.

Create `test/test_manual_spool_loading.py` with an LVGL and FilaMan API shim for `manual_spool_screen.cpp`. Cover a successful page fetch, a page-fetch HTTP failure, a successful selected-spool lookup, and a failed selected-spool lookup. For each blocking call, assert `loading_shown` increased once, `loading_hidden` increased once, and `loading_depth` returned to zero.

- [ ] **Step 2: Run the harness and verify missing generic/loading behavior**

Run:

```bash
python3 test/test_filaman_print_deferred.py
python3 test/test_manual_spool_loading.py
```

Expected: both FAIL because the production label screen directly reads M220 preferences and neither new screen consistently uses the loading overlay.

- [ ] **Step 3: Make raster fetch and preview model-aware**

In `label_print_screen.cpp`, replace the two media globals with one `LabelPrinterConfig preview_printer`. At preview fetch time:

```cpp
preview_printer = labelPrinterLoadConfig();
const uint16_t width = labelPrinterRasterWidth(
    preview_printer.model, preview_printer.media_width_mm);
const char* orientation =
    preview_printer.media_width_mm >= preview_printer.media_length_mm
      ? "landscape" : "portrait";
```

Use `labelPrinterRasterFits()` for preview-button enablement and again immediately before print. If the user changes printer settings after preview generation, the second check must fail rather than sending a raster built for the old configuration.

Replace M220-specific label identifiers and visible text with generic printer equivalents. The BLE print button text is `Print`; the busy string is formatted as `Sending to %s...` with `labelPrinterProfile(preview_printer.model).name` in a fixed local buffer.

- [ ] **Step 4: Add loading overlays to presets, preview, BLE print, and PC request**

Use the existing overlay around every blocking operation:

```cpp
loadingOverlayShow(T(STR_LABEL_LOADING));
int code;
{
  HttpStall stall(loadingOverlayProgress);
  code = filamanListLabelPresets(backendBaseUrl(), filamanApiKey(),
                                 presets, kPresetCapacity, &preset_count);
}
loadingOverlayHide();
```

Use the same shape for `filamanRequestLabelPrint()`. For `filamanFetchMonoLabel()`, keep the overlay visible through `drawPreview()` and remove it before setting the final success or error status. For BLE printing:

```cpp
loadingOverlayShow(send_message);
const LabelPrinterConfig current = labelPrinterLoadConfig();
const bool sent = labelPrinterPrint(current, preview, error, sizeof(error),
                                    loadingOverlayTick);
loadingOverlayHide();
```

Do not add a second spinner or timer. The existing overlay blocks touches and paints immediately; the passed progress functions advance it during network reads and BLE chunks.

In `manual_spool_screen.cpp`, wrap `filamanGetSpoolPageJson()` with `loadingOverlayShow(T(STR_SPOOLS_LOADING))`, `HttpStall(loadingOverlayProgress)`, and `loadingOverlayHide()`. Wrap `querySpoolmanById()` with the same show/hide pair using `STR_SPOOLS_OPENING`. WiFi and NFC validation stays before showing the overlay; both success and failure status rendering happens after hiding it.

- [ ] **Step 5: Feed HTTP raster bytes into the existing progress hook**

In `filaman_labels.cpp`, replace the direct raster read loop with a `Stream*` that uses `HttpProgressStream` only when a UI hook is active:

```cpp
WiFiClient* raw = http.getStreamPtr();
HttpProgressStream progress(*raw);
Stream* input = httpProgressActive()
    ? static_cast<Stream*>(&progress)
    : static_cast<Stream*>(raw);
size_t used = 0;
while (used < image.length) {
  const size_t got = input->readBytes(image.pixels + used, image.length - used);
  if (!got) break;
  used += got;
}
```

Keep validation against surplus bytes on the raw socket. `HttpStall` in the UI owns and clears the hook; the API function retains its existing `HttpStallTime` timing bracket.

- [ ] **Step 6: Remove direct M220 preferences from other UI consumers**

Update:

- `header_status.cpp`: use `labelPrinterConfigured(labelPrinterLoadConfig())` for glyph visibility;
- `connection_screen.cpp`: show the generic saved name/address and model;
- `settings_screen.cpp`: show Print Labels only when the generic configuration is complete and FilaMan is active.

Delete the temporary `phomemo_m220.h` compatibility header and its two wrapper functions from `phomemo_m_series.cpp` after these consumers compile through `label_printer.h`.

Run:

```bash
rg -n 'm220_addr|m220_name|m220_media_[wh]|phomemoM220|M220Device' src
```

Expected: only the four legacy preference reads in `label_printer.cpp` remain. No UI file directly knows an M220 preference key or transport function.

- [ ] **Step 7: Verify every new blocking screen state has an indicator**

Audit with:

```bash
rg -n "filamanListLabelPresets|filamanFetchMonoLabel|filamanRequestLabelPrint|labelPrinterScan|labelPrinterPrint|loadingOverlay" \
  src/ui/label_print_screen.cpp src/ui/printer_settings_screen.cpp src/ui/manual_spool_screen.cpp
```

Confirm:

- spool-page loading and selected-spool opening pair show/hide;
- preset loading pairs show/hide;
- preview rendering pairs show/hide;
- BLE scan pairs show/hide;
- PC print request pairs show/hide;
- BLE print pairs show/hide.

Every early validation failure occurs before `loadingOverlayShow()`, and every result after showing it passes through `loadingOverlayHide()`.

- [ ] **Step 8: Run label-flow regression tests**

Run:

```bash
python3 test/test_filaman_print_deferred.py
python3 test/test_manual_spool_loading.py
python3 test/test_filaman_print_flow.py
python3 test/test_filaman_label_presets.py
python3 test/test_filaman_print_request.py
```

Expected: PASS, including failure cleanup and the existing duplicate-PC-request guard.

- [ ] **Step 9: Commit generic print routing and loading feedback**

```bash
git add src/ui/label_print_screen.cpp src/services/filaman_labels.cpp \
  src/ui/header_status.cpp src/ui/connection_screen.cpp src/ui/settings_screen.cpp \
  src/ui/manual_spool_screen.cpp \
  src/services/phomemo_m_series.cpp src/services/phomemo_m220.h \
  src/lang.h src/lang.cpp test/test_filaman_print_deferred.py \
  test/test_filaman_print_flow.py test/test_manual_spool_loading.py
git commit -m "Route label printing through modular drivers"
```

### Task 5: Documentation, full verification, flash, and hardware checks

**Files:**
- Modify: `README.md`
- Verify only: all production and test files changed in Tasks 1-4

**Interfaces:**
- Consumes: completed modular printer feature.
- Produces: user-facing setup guidance, a firmware binary within the OTA limit, a reverified M220 path, and an M110 external-test checklist.

- [ ] **Step 1: Update the FilaMan printing documentation**

Replace the M220-only paragraph in `README.md` with concise instructions that say:

- open Settings -> Connection -> Printer;
- select a printer model separately from the BLE device;
- M220 is the default and hardware-verified model;
- M110 is experimental and supports presets up to 48 mm across the print head;
- scanning displays all captured BLE devices and merely prioritizes likely Phomemo names;
- loaded label size remains width across the printer x feed length;
- no printer is selected by default.

Keep the existing links to the upstream repository documentation and FilaMan.

- [ ] **Step 2: Run every focused host check**

Run:

```bash
python3 test/test_label_printer.py
python3 test/test_printer_settings.py
python3 test/test_filaman_print_deferred.py
python3 test/test_manual_spool_loading.py
python3 test/test_filaman_print_flow.py
python3 test/test_filaman_label_presets.py
python3 test/test_filaman_print_request.py
g++ -std=c++11 -Isrc test/label_protocol_selftest.cpp -o /tmp/label_protocol_selftest
/tmp/label_protocol_selftest
```

Expected: all tests PASS.

- [ ] **Step 3: Build the firmware and enforce repository checks**

Run:

```bash
pio run -e wt32-sc01-plus
scripts/check.sh
wc -c .pio/build/wt32-sc01-plus/firmware.bin
```

Expected:

- PlatformIO reports SUCCESS;
- `scripts/check.sh` reports no failure;
- `firmware.bin` is at most 3,145,728 bytes;
- static RAM use does not materially increase beyond the 212,760-byte baseline except for the bounded 24-device scan array.

- [ ] **Step 4: Inspect the final diff for model leaks and loading leaks**

Run:

```bash
git diff --check
rg -n 'm220_addr|m220_name|m220_media_[wh]|phomemoM220|M220Device' src
rg -n 'loadingOverlayShow|loadingOverlayHide' \
  src/ui/label_print_screen.cpp src/ui/printer_settings_screen.cpp src/ui/manual_spool_screen.cpp
git status --short
```

Expected: clean whitespace; only migration reads mention old preference keys; no direct old driver symbols; each new blocking UI action has paired loading calls; only intended files are modified.

- [ ] **Step 5: Commit the documentation after verification**

```bash
git add README.md
git commit -m "Document modular Bluetooth label printers"
```

- [ ] **Step 6: Flash the connected scale**

Discover the current port rather than assuming it retained the old number:

```bash
pio device list
pio run -e wt32-sc01-plus -t upload --upload-port /dev/cu.usbmodem2101
```

If `pio device list` reports a different `/dev/cu.usbmodem*` port, use that exact port. Expected: upload SUCCESS and the scale reboots to the normal home screen.

- [ ] **Step 7: Reverify the M220 on hardware**

On the scale:

1. Open Settings -> Connection -> Printer and confirm M220 is selected by default.
2. Start a scan and confirm the loading overlay is immediately visible and animated.
3. Confirm the saved device is first; confirm unrelated and unnamed BLE devices remain selectable.
4. Select the known `Q...` printer, choose the physical 40 x 30 mm loaded label, and return to the print flow.
5. Load spools, load presets, and render a preview; confirm every wait shows and clears its loading overlay.
6. Print the known 40 x 30 preset and confirm exactly one nonblank label prints in the correct orientation.
7. Turn the printer off and retry; confirm the connection error replaces the overlay and the Print button can be tried again.

- [ ] **Step 8: Prepare the M110 external-test handoff**

Give the external tester this exact checklist:

1. Select M110 explicitly; do not rely on the BLE name.
2. Scan and select the M110 address, including an unfamiliar or `Q...` advertised name.
3. Load a gap label no wider than 48 mm and select its width/feed length.
4. Preview a matching monochrome preset and confirm orientation.
5. Print once and report connection success, blank/nonblank output, feed count, rotation, left/right alignment, and measured printed dimensions.
6. Capture the scale serial log if connection or writing fails.

Keep the M110 UI badge experimental until all six checks pass on real hardware.
