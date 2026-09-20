# Modular label printer support

## Goal

SpoolmanScale shall support more than one label printer without coupling the
touchscreen, settings, or FilaMan label flow to a specific model. The first
models are the proven Phomemo M220 and an experimental Phomemo M110. A user
selects both the printer model and the Bluetooth device because advertised
Bluetooth names are not a reliable model identifier.

The firmware shall include these small drivers in the normal build. Printer
drivers are not separately installed. The current build uses 2,870,873 of
5,242,880 application bytes, and the M110 reuses the existing BLE and raster
path. If a future printer requires a large SDK, firmware size can be
reconsidered then.

## User experience

Settings -> Connection -> Printer contains three sections:

1. **Printer model.** M220 is the default. M110 can be selected and is marked
   experimental until it is verified on hardware.
2. **Bluetooth device.** The saved device is shown separately. Scan lists all
   nearby BLE devices; names never exclude a device. The saved address sorts
   first, names resembling the currently observed Phomemo `Q...` format or
   containing a selected model name sort next, and all remaining devices
   follow. Tapping a row selects that address without changing the model.
3. **Loaded label size.** Width across the print head and feed length remain
   large touchscreen controls. Values are validated against the selected
   model. M220 keeps its current limits. Initial M110 support accepts label
   designs up to its 384-dot, approximately 48 mm printable width; wider M110
   stock requires a preset whose printed design fits that area.

No printer is configured by default on a fresh installation. Existing saved
M220 settings continue to work and are migrated to the generic printer
settings when the user saves the page.

Every asynchronous screen state added by the label printing feature shall
show a visible loading indicator and a short activity label. This applies to
loading spools, loading presets, Bluetooth scanning, rendering a preview, and
sending a print job. While busy, the action that started the work is disabled
so duplicate requests cannot be queued. Success, empty, and error states
replace the indicator when work ends.

## Firmware architecture

The UI uses one printer service and does not read model-specific preference
keys or call an M220 function directly. The service owns a compact
`PrinterConfig` containing:

- model (`none`, `m220`, or `m110`);
- BLE address and advertised name;
- loaded media width and feed length.

A small model table supplies the display name, default media size, allowed
dimensions, print-head width, and experimental status. Dispatch uses an enum
and a switch. Virtual classes, a factory, and dynamic driver registration are
unnecessary on this device.

The Phomemo M-series code is divided by responsibility:

- common BLE code scans, connects to service `0xff00`, finds write
  characteristic `0xff02`, sends bounded chunks, waits for completion, and
  performs safe disconnect cleanup;
- the M220 protocol keeps the exact command sequence, chunking, and timing
  already verified on the connected printer;
- the M110 protocol supplies its documented M-series command sequence and
  384-dot print-head limit without changing the M220 path.

The common printer service is the only surface consumed by the touchscreen,
header status, and label flow. A future printer adds one model entry and one
dispatch case. A printer outside the Phomemo M-series can provide a separate
transport behind that same service without changing the UI.

## Data flow

1. The user chooses a model and a scanned BLE address in Printer Settings.
2. The generic service persists the complete configuration.
3. The label flow asks the selected model for its raster width and validates
   the loaded size.
4. FilaMan renders the existing monochrome `mono1` response at 203 DPI.
5. The printer service validates the raster and dispatches it to the selected
   model protocol over the shared BLE transport.
6. The screen shows a spinner and progress text until the operation succeeds
   or returns an actionable error.

Bluetooth names are sorting hints only. They do not select a model, filter a
scan result, or determine which protocol sends the job.

## Errors and status

Printing is refused before connecting when the model, BLE address, loaded
size, or raster dimensions are invalid. Connection, service discovery, write,
disconnect, FilaMan, and allocation failures return concise touchscreen
messages. Model names in messages come from the selected profile rather than
being hard-coded as M220.

The header Bluetooth glyph continues to indicate a configured printer and
uses the generic configuration. Live connection status remains limited to an
active print connection because the printer is not kept connected between
jobs.

## Verification

- Host checks cover model defaults, dimension limits, raster width, generic
  configuration migration, and deterministic scan ordering.
- The existing M220 print path is built, flashed, and printed on the connected
  hardware after refactoring.
- Each loading state is inspected on the touchscreen and confirmed to clear on
  success, empty responses, and errors.
- M110 builds and is covered by protocol byte checks, but remains marked
  experimental and unverified until an external tester confirms discovery,
  connection, raster orientation, feed, and printed dimensions on an M110.
- The normal firmware-size gate remains in force, including compatibility
  with devices that still have 3 MB OTA slots.

## References

- [SpoolmanScale repository](https://github.com/Niko11111/SpoolmanScale)
- [SpoolmanScale documentation](https://niko11111.github.io/SpoolmanScale-Docs/)
- [myphomemo M-series implementation](https://github.com/DeepCoreSystem/myphomemo)
- [phomemo-tools protocol notes](https://github.com/vivier/phomemo-tools)
