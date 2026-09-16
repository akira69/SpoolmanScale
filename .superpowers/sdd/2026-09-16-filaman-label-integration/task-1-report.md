# Task 1 report: FilaMan label preset picker

## Delivered

- Added `filamanListLabelPresets()` with `ApiKey` authentication, an 8 KiB bounded response, strict array/id/name validation, and an explicit capacity error.
- Added the FilaMan-only More Info header action and a scrollable default-plus-presets selector. The selected id is stored under `label_preset`; a deleted saved preset is reset to `0` with a notice.
- Network work is deferred from LVGL callbacks through the application loop. Navigation releases the selector and clears pointers and pending work.

## TDD evidence

RED: `python3 test/test_filaman_label_presets.py` failed with `AssertionError` because `FilaManLabelPreset` did not yet exist.

GREEN: after implementation, `python3 test/test_filaman_label_presets.py` passed. It checks the API declaration and bounded authenticated endpoint contract.

## Verification

Passed:

```text
PLATFORMIO_CORE_DIR=/tmp/filaman-scale-platformio-core \
PLATFORMIO_SETTING_ENABLE_TELEMETRY=no pio run -e wt32-sc01-plus
```

The final build used 196,416 / 327,680 bytes RAM and 2,165,793 / 3,145,728 bytes flash.

## Concerns

- Device inspection is unverified because no target device was available. Empty lists, multiple and long names, and no WiFi need physical verification.
- Task 2 has not yet implemented a label output action; this task only persists the selected ID for it.

## Review fixes

Fixed saved-preset selection to read LVGL event user data, which is where the row ID is stored. Parsing now keeps `count` at zero until every returned entry validates.

RED: the replacement behavioral test failed before the helper existed:

```text
fatal error: 'services/filaman_label_preset_parse.h' file not found
```

GREEN command and output:

```text
python3 test/test_filaman_label_presets.py
# exit 0

PLATFORMIO_CORE_DIR=/tmp/filaman-scale-platformio-core \
PLATFORMIO_SETTING_ENABLE_TELEMETRY=no pio run -e wt32-sc01-plus
# SUCCESS; RAM 196416 / 327680, Flash 2164549 / 3145728
```

The host check compiles and runs the production preset parser. It verifies a nonzero selected ID remains `7`, and a later invalid preset rejects the list with `count == 0`.

## Callback test fix

The focused test now compiles and invokes the production `labelPresetRowCb()` with event data `7` and separate object data `99`; `prefsPutInt()` receives `7`. Reverting the callback to target-object user data makes that assertion fail.

RED command and output:

```text
python3 test/test_filaman_label_presets.py
# clang++: error: no such file or directory: src/ui/label_preset_selection.cpp
```

GREEN command and output:

```text
python3 test/test_filaman_label_presets.py
# exit 0

PLATFORMIO_CORE_DIR=/tmp/filaman-scale-platformio-core \
PLATFORMIO_SETTING_ENABLE_TELEMETRY=no pio run -e wt32-sc01-plus
# SUCCESS; RAM 196416 / 327680, Flash 2164569 / 3145728
```
