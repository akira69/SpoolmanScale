# Touchscreen Spools and Printer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Select and print a FilaMan spool on the scale without NFC, with printer setup under Connection.

**Architecture:** A FilaMan-only spool screen reuses the inventory and by-ID lookup. A printer settings screen owns M220 scan, address, and width preferences. The existing label screen consumes these preferences. The main header reads printer configuration from NVS.

**Tech Stack:** ESP32-S3, Arduino, LVGL 8, PlatformIO.

**Spec:** `docs/superpowers/specs/2026-09-16-touchscreen-spools-printer-design.md`

## Global Constraints

- Default has no printer configured.
- Bluetooth glyph appears only for active FilaMan with a saved printer address.
- Manual spool selection must not set NFC or automatic weighing state.

---

### Task 1: Add NFC-free spool selection

**Files:** `src/ui/manual_spool_screen.{h,cpp}`, `src/ui/main_screen.cpp`, `src/ui/navigation.cpp`, `src/app/app_loop.cpp`.

- [x] Add a main-screen Spools affordance when FilaMan is active and no tag is present.
- [x] Build an active-spool list using the current backend API, with a safe LVGL row limit.
- [x] Select by ID using the current by-ID lookup, display errors, and open existing details.
- [x] Run PlatformIO compile and check no duplicate or missing symbols.

### Task 2: Move M220 configuration into Connection settings

**Files:** `src/ui/printer_settings_screen.{h,cpp}`, `src/ui/connection_screen.cpp`, `src/ui/label_print_screen.cpp`, `src/ui/navigation.cpp`, `src/app/app_loop.cpp`.

- [x] Add a FilaMan-only Printer tile and screen with scan, select, clear, and width.
- [x] Remove scanner and width controls from the label screen; read persisted width there.
- [x] Confirm PC and BLE print still use selected preset and spool ID by code review; physical M220 print requires a printer.

### Task 3: Header status and verification

**Files:** `src/app/app_state.{h,cpp}`, `src/ui/main_screen.cpp`, `src/ui/header_status.cpp`.

- [x] Add the existing font's Bluetooth glyph beside WiFi, conditional on FilaMan and saved address.
- [x] Refresh when printer selection changes and when backend changes.
- [x] Build firmware and flash the attached test ESP32-S3; read boot output.
