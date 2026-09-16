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
