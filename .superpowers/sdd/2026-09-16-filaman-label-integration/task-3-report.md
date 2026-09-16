# Task 3 report

## Change

Added `LabelRaster` and shape/padding validation. The mono1 fetcher requests the selected spool and preset, checks numeric and bit-order headers, requires exact `Content-Length`, allocates only in PSRAM, reads the declared body, and returns an image only after validation. Failure clears the output and frees its allocation. It does not send printer data.

## RED / GREEN

- RED: `c++ -std=c++17 -Isrc test/label_protocol_selftest.cpp -o /tmp/label_protocol_selftest && /tmp/label_protocol_selftest` exited 1: `fatal error: 'services/label_raster.h' file not found`.
- GREEN: same command exited 0, no output. Cases: valid 480×320, short body, incorrect row stride, zero width, valid/invalid padding for 385 pixels.
- `git diff --cached --check` exited 0.

## Build and device checks

- `pio run -e wt32-sc01-plus` could not start because sandbox denied writing `/Users/dfinch/.platformio`.
- `PLATFORMIO_CORE_DIR=/private/tmp/filaman-pio pio run -e wt32-sc01-plus` started and reached `Platform Manager: Installing espressif32 @ 6.13.0`; installation stalled and was aborted.
- An approved `pio run -e wt32-sc01-plus` installed the platform, toolchain, and framework, then failed before compilation while installing `tool-esptoolpy`: Python 3.14 `package-postinstall.py` died with `SIGSEGV`.
- No configured FilaMan server or physical scale/printer was available for default/saved preset fetches or controlled malformed HTTP responses. No printer bytes can be emitted by this fetcher because it has no printer dependency.

## Files

- `src/services/label_raster.h`
- `src/services/filaman_labels.h`
- `src/services/filaman_labels.cpp`
- `test/label_protocol_selftest.cpp`

## Concerns

Extra bytes beyond a truthful `Content-Length` are caught only if already buffered when the expected body ends. HTTP framing leaves later surplus bytes outside the declared response. A device integration check remains necessary, including default and saved presets at 576 pixels.
