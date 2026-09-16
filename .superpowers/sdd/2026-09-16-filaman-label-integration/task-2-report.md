# Task 2 report — FilaMan PC print request

## Implemented

- Added `filamanRequestLabelPrint` using the user API key and `POST /api/v1/labels/spool/{id}/print-request`, omitting `preset_id` for the default label. A 201 is accepted only with a positive request ID, matching spool ID, and matching or null preset ID. No automatic retry.
- Added **Open on PC** to the preset screen. The tap freezes spool and preset IDs, sets pending immediately, ignores repeat taps while pending, and sends from the app loop under `HttpStallTime`.
- The screen says the request is queued for approval in the FilaMan tab and directs the user to Print or Export PDF there. 401, 403, 404, 422, and network failures show recovery text. The backend key help now says user key, `spools:read`, and same signed-in PC account.

## RED / GREEN evidence

- RED: `python3 test/test_filaman_print_request.py` failed at compile with missing `services/filaman_print_request_parse.h`.
- GREEN: same command exited 0 after implementation; checks valid default and selected preset responses plus invalid IDs, mismatches, and malformed JSON.
- Additional RED: adding a missing `preset_id` response assertion failed at runtime; validator then required that field.
- Final: `python3 test/test_filaman_print_request.py` and `python3 test/test_filaman_label_presets.py` exited 0.
- Final firmware: `PLATFORMIO_CORE_DIR=/tmp/filaman-scale-platformio-core PLATFORMIO_SETTING_ENABLE_TELEMETRY=no pio run -e wt32-sc01-plus` exited 0, `[SUCCESS] Took 9.13 seconds`.
- `git diff --check` exited 0.

## Integration limits / concerns

- Local server at `127.0.0.1:8000` returned 404 to an unauthenticated POST for this endpoint. No user credentials or signed-in PC tab were available. Same-user prompt, different-user isolation, one-request double tap on hardware, PDF export, and unreachable-server UI have not been manually verified.
- `HTTPClient::POST` is called once. A negative transport result is shown as failure with a prompt to inspect the FilaMan tab before sending again, because the server may have queued a request despite a timeout.
