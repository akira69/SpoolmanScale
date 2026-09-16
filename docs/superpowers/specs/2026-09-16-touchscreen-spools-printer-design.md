# Touchscreen spools and printer setup

FilaMan users can open a Spools page from the scale touchscreen without an NFC tag. The page lists active spools from the existing FilaMan API, 20 at a time, with previous and next controls. Selecting a row clears stale tag data, loads that spool through the existing single-spool lookup, and opens its detail view, where Print label already exists. A failed lookup keeps the list open with an error. Manual selection must not claim that an NFC tag is present or start automatic weighing.

Printer setup lives under Settings → Connection → Printer. It shows the saved M220 address, scans nearby printers, saves a selected address, allows clearing it, and keeps the existing printable-width adjustment. No printer is configured by default. The label screen retains preset selection, PC print, and Bluetooth print, reading the saved address and width. FilaMan renders the requested label; the scale sends its monochrome raster to the configured M220 over BLE.

The header shows a Bluetooth glyph beside WiFi only when FilaMan is the active backend and an M220 address is saved. Switching backend or clearing the printer hides it. The glyph indicates configuration, not a persistent BLE connection.

This uses the existing FilaMan spool list, single-spool lookup, NVS preferences, and M220 driver. It adds no server API or development-only mode.
