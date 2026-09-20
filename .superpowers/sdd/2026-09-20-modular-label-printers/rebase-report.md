# Rebase report - 2026-09-20

## Result

- Worktree: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale`.
- Branch: `feat/filaman-label-integration`.
- Refetched `upstream dev`; fetched SHA: `2a8c12453eb8476952e8816b6f4b720fe2f0c1f6`.
- Original clean tip and retained safety branch `backup/feat-filaman-label-integration-pre-rebase-20260920`: `f6996b88baa080fc8ebeb5e0d2953d120e9ddd1a`.
- Ran normal `git rebase upstream/dev`, replaying 41 non-merge commits. Rewritten feature tip before the integration corrections recorded with this report: `2b4f9e6a910ca7636467ec664c42041d818b3299`.
- Historical merge `5cce6092f8e6ed04933070ea6cc37e5e0742b341` was flattened as expected. Its manual resolutions were audited against the old final tree; missing merge-only intent was restored in the integration correction commit containing this report.
- No push or flash performed. Plan, spec, and ledger contents were not edited.

## Conflicts and exact resolutions

| Replayed commit | Conflicted files | Resolution |
|---|---|---|
| `150ded6` Add FilaMan label preset picker | `src/lang.cpp`, `src/ui/more_info_screen.cpp` | Inserted the label rows before the location-toggle rows, retaining upstream French translations. Kept upstream theme include/shared `buildStatusChip` callback; added the feature label include and print button after the shared chip. All upstream unlink/server-reach logic retained. |
| `e2e3c6d` Add FilaMan PC label print request | `src/lang.cpp` | Added the PC-request strings alongside upstream French location/drying rows. Kept upstream's corrected separate API-key/device-code setup instructions and added scope/same-user PC-tab guidance. Final setup row restored from the old integrated feature in all three languages. |
| `3d55979` Cancel hidden PC print requests and test wire flow | `src/lang.cpp` | Added the German API scope and signed-in PC guidance without reverting corrected upstream setup instructions; final multilingual row matches the old feature. |
| `5398f6f` Add M220 BLE label printing | `src/lang.cpp` | Added all M220 rows and preserved upstream French location/drying entries. |
| `6ac1019` Report unavailable PSRAM during label fetch | `src/lang.cpp` | Added the PSRAM error row; retained upstream French rows. |
| `0b591ff` Add NFC-free FilaMan spool printing setup | `src/app/app_loop.cpp`, `src/lang.cpp`, `src/ui/spoolman_lookup.cpp` | Kept upstream `newly_placed` detection and wake rules while adding feature header-status refreshes. After independent review, intentionally did not retain the historical unconditional Bambu `TagSeen::note` block: upstream records only the later settled classification, and UID deduplication would otherwise mask Snapmaker/MIFARE. Added spool/printer strings without losing French. Retained upstream server-color helper and lookup logic while changing `querySpoolmanById` to return false on failure and true on success for manual loading. |
| `a7a5e12` Preview and print physical 40x30 M220 labels | `src/ui/ui_common.cpp` | Added `styleOutlineButton`; retained upstream `swatchHue`/SpoolColor implementation rather than resurrecting the obsolete hex swatch function. |
| `c6117dc` Show configured Bluetooth printer as idle | `src/ui/main_screen.cpp` | Set Bluetooth label to feature's `UI_COL_IDLE`. |
| `ed83949` Match printer screens to scale theme | `src/ui/label_print_screen.cpp`, `src/ui/manual_spool_screen.cpp`, `src/ui/printer_settings_screen.cpp`, `src/ui/ui_common.cpp`, `test/test_filaman_print_deferred.py` | Applied the feature's list-panel/list-row/outline style helpers and matching test shims; retained upstream swatch implementation. |
| `e1da91c` Add loaded label size printer setup | `src/lang.cpp`, `src/ui/printer_settings_screen.cpp`, `test/test_filaman_print_deferred.py`, `tools/lang_fr.jsonl` | Accepted the feature's newly translated label/spool/printer rows and media-size controls. Updated raster-fetch test signature. Merged JSONL by StringID/field, retaining upstream records and applying feature changes. |
| `d25f64d` Add printer model and BLE device selection | `tools/lang_fr.jsonl` | Merged JSONL structurally by StringID/field, preserving upstream rows and adding model/device-selection records. |
| `d8f1c79` Route label printing through modular drivers | `test/test_filaman_print_deferred.py`, `tools/lang_fr.jsonl` | Took this feature-only test file's current modular-driver harness in full, including the LVGL shim and loading/error checks. Structurally merged metadata; removed only feature-renamed M220 IDs and retained upstream IDs. |
| `a6524d0` Correct label preview controls and status layout | `src/ui/label_print_screen.cpp` | Applied feature's theme color, y=50, 40-pixel status height, body font and ellipsis mode. |
| `f028b53` Fit French preset caption within preview control | `tools/lang_fr.jsonl` | Applied the feature's shorter French preset caption by ID; retained all other upstream/feature metadata. |

## Final audit and automatic merges

- `app_loop.cpp`: retained upstream flash-log maintenance, server-reach popup after deferred handlers, restored-reach notification and shared failure-aware `paintTagStatus`. Feature label/manual-spool/printer handlers, pending-NFC-clear cancellation, tag header updates remain. Upstream settled Bambu/Snapmaker/MIFARE classification is retained; the unconditional Bambu recording was removed after independent review.
- `lang.h` / `lang.cpp`: table is the exact union of 939 upstream IDs and 976 old-feature IDs: 995 unique rows. All upstream string values are unchanged except the intentionally combined `STR_W_FM_SETUP`; all feature label, printer and spool strings match the old tip. There are 19 newly added upstream IDs.
- `tools/lang_fr.jsonl`: rebuilt from upstream metadata plus feature human-field changes by ID, then ran existing `lang_fr.py seed` and `emit` to refresh positions, budgets and table formatting. Exact metadata/table ID match; no lost or duplicate rows. Retained all upstream translations and unfinished statuses, all feature French translations, and scope/PC-tab setup text. 962 translations and 33 English fallbacks.
- `filaman_api.cpp` / `.h`: label-preset and paginated-spool APIs coexist with upstream named heartbeat timing, second-RFID-slot probe HTTP-code tracking and network-failure propagation. Shared API-key helper remains. `http_progress.cpp` / `.h` are byte-identical to upstream; existing feature `HttpStall` callers remain compatible with upstream optional timing names and hook lifetime.
- `prefs_store.cpp` / `.h`: upstream `prefsHasKey` keeps queued-write awareness. Feature empty-string writes reopen storage, require key existence and verify persisted empty value before success. Printer deferred-save gate remains covered by host checks.
- `more_info_screen.cpp`: upstream server-reach note calls, abort-on-network-failed unlink, retained binding and status message, logging and shared status chip remain. FilaMan Print label still opens `requestLabelPreviewScreen(sm_id)` from the header. Removed one duplicate theme include introduced during replay.
- `spoolman_lookup.cpp`: only feature's boolean return contract differs from upstream; current upstream direct-ID fallback, network verdict handling, color resolution, and remote-link follow-up behavior remain.
- Restored the old merge-only theme includes/colors in label, manual-spool, printer and main screens. Those feature screens now match the old feature byte-for-byte.
- Preserved old merge-only whitespace cleanup in French supplement fonts and Snapmaker sources. Restored pre-rebase `manifest.json` after the build hook rewrote it, avoiding a release-artifact/version change in this integration. No tracked firmware binary changed.

## Verification

- All eight `test/test_*.py` scripts passed: label preset parsing/selection, PC print request parsing/wire flow, deferred print/UI/loading/media handling, generic printer configuration/migration, manual spool loading, preference persistence and printer settings UI.
- `c++ -std=c++17 -Isrc test/label_protocol_selftest.cpp -o /tmp/filaman-label-protocol-rebase && /tmp/filaman-label-protocol-rebase`: passed.
- `c++ -std=c++17 -Wall -Wextra -Werror -Isrc -I.pio/libdeps/wt32-sc01-plus/ArduinoJson/src tests/label_preset_parse_test.cpp -o /tmp/filaman-label-preset-rebase && /tmp/filaman-label-preset-rebase`: passed.
- An initial optional `-Werror` protocol-test compile rejected its existing partially initialized aggregate; the normal C++17 self-check command above passed without changing tests.
- `PYTHONPATH=/private/tmp/spoolmanscale-pio-deps pio run -e wt32-sc01-plus`: final build passed. RAM 214,048 / 327,680 bytes; ELF flash usage 2,886,945 / 5,242,880 bytes. The existing temporary Python dependency directory supplies `intelhex`; the default Homebrew invocation failed without it. PlatformIO cache access needed sandbox escalation and was approved.
- `bash scripts/check.sh`: passed with warnings after the final build. 995 rows match the enum; 0 French errors; all 70 hard width budgets passed; JS parsing and convention ratchet passed. Remaining warnings are the existing UTF-8 log truncation candidate, large-file growth and French advisory warnings. Firmware fits the 3 MiB legacy OTA app slot.
- Structural language assertions verified the union, every upstream value except merged setup, feature string preservation, and exact metadata ID set. `git diff --check` passed.
- No hardware exercise or flashing was performed; physical printer/scale behavior is not newly verified by this rebase.

## Divergence and commit mapping

After the separate review-fix commit removing the unconditional Bambu record, `upstream/dev...HEAD` is **0 upstream-only, 43 local-only**. Relative to the existing local remote-tracking `origin/feat/filaman-label-integration` (`e1da91cec73744aa5427f0c7369a4fcbb1b11367`), divergence is **29 origin-only, 46 local-only**. Origin was not fetched or pushed for this task. The backup branch remains at the original tip.

The 41 ordinary feature commits were matched by unique commit subject. The old merge commit is deliberately absent in the linear history; the separate integration correction commit carries audited merge-only intent and this report.

| Old commit | Rewritten commit | Subject |
|---|---|---|
| `ec0da1d296d028fbf3789a652273f2de1b14e145` | `c78feb98cb4c37e4fedabae9c1fa8d6a86416725` | docs: plan FilaMan label integration for scale |
| `150ded601c17531f1f3b596467383588a5c1828a` | `6fce58aaee6819e96542b09a736209f3cffff469` | Add FilaMan label preset picker |
| `79b86ba29fa486e08b79337088a5a15327060715` | `e0d6013bdf0ac1f42e939acc0b017e530d1110af` | Fix FilaMan preset selection and validation |
| `d6a734794251d32314fa50691a1cafdca538e258` | `a0fd5e0aa1d862b2838501d4997369d5e23c64fd` | Exercise label preset callback in tests |
| `72cc52891c66119e32e9cad5a452d0ccfe1ccb49` | `ef571d13ff86994bf0070e1fa60d30696b1b126d` | Remove unused preset selection helper |
| `e2e3c6db940154cba32f93b0977da6dd9bbf85b0` | `56b9d6787389f2b27e8cb869b9db78e129826c73` | Add FilaMan PC label print request |
| `3d55979ab9b053100634ab7dfbdf30fd20b828b9` | `00e02029a68b9d9e433b6c10c96d36ba370bb9f8` | Cancel hidden PC print requests and test wire flow |
| `da9455c73554b0a2bc461b0b1e4c37302ff62fea` | `c319bae8d656b96f31d9d153931f0facd07f28c2` | Test deferred PC print navigation behavior |
| `0bf49e22d8ca87f3ecf82f9a436d94dd6a3872f4` | `6efb7a6594cb586d91fd15c838bfc75d1c4244df` | Validate and fetch FilaMan mono label raster |
| `5398f6f8e588da54f2e7a1c85f800644bb7c56c2` | `c4cdfd22ea3cdf508f0532d2dbb68bb308441e02` | Add M220 BLE label printing |
| `afa01ef59f46691fb49f5391fd99cb3e08db183f` | `cfa185b52af734c0c47bbf097981763af9e4e8a7` | Report BLE write errors and preserve label screen state |
| `4463bbcc5cdd6df8fcd05f2e62f0af5d3aebce6b` | `6f4cbfc0559eeab0d9b98efe44b34f26613d6307` | Wait for M220 GATT write completion |
| `c46473711b69212026c0be520ab2aa42a00c1754` | `1437697fdf8ed53f424d5b9ba14474471abc7ee4` | Clarify label failures and refresh deferred host check |
| `6ac1019049143e167268f467cad01df79eb5cad9` | `c334047242b07cc34a4eea1e3352024e4324d3c2` | Report unavailable PSRAM during label fetch |
| `a78917e04133bf1e29a322cfb8054a5273f0bc79` | `21d1b9d547894053eff4ebdb4d2b5fb1441d67b1` | Wait for BLE disconnect and refresh selected label preset |
| `527eb8cc2bd1954e7f38e4dea478d230e0fc0a55` | `d946294345ea499beac205a6360b9ec7e8f4eeeb` | Allow safe retry after failed M220 connection |
| `7d07387b5889c3b179736afda16e026173895447` | `dbb37835eb8ee9bdcd519a1bfa2a64a64e510fdd` | Wait for zero-valued BLE registration event |
| `0b591ffb6593c49bd753accb50c14a363698bb9e` | `cb5650e173a116bc2b4731987551d123638693ab` | Add NFC-free FilaMan spool printing setup |
| `a7a5e12b5cd4234c03ea18e0b06bf8583fbc4f26` | `cc09242650075e8aa2d09e8af53b1b1a980a308e` | Preview and print physical 40x30 M220 labels |
| `7815c0231b197b153056ddeeecd335009f1efe9b` | `d70273c342c032c8434693c9420c3a95d6e09dc6` | Group large label preset lists on the scale |
| `c6117dc2b9cd5c2dc1920566f066c69f297954e4` | `6ffc305e4bd2f2064439495f538c00a5260cc81c` | Show configured Bluetooth printer as idle |
| `ed83949c26374158502499c5d7515385ab499563` | `89a52c711f03c866f8a19e948186e6d06431d27c` | Match printer screens to scale theme |
| `25c8b340c4fc2edc02a2ff86065a2d6ac9f6bc63` | `6e11950b534b9b4a63ac0d44f94911666c263f33` | Move label printing into configured settings |
| `e1e53c743e697454677c0d4aa52317a1a712ac9c` | `2fee4269a32c5c0b9ee07f3103111756e8a5a150` | Keep Settings print action visible |
| `40bcc35666e4bb26b757c145c776ce5ed36d478b` | `18ad7fcc19ea31d25967cbe2ee7dde182dd9f6bd` | Fit print action on Settings screen |
| `44e82df4ab22fbf9953bb3125d56a41256f25018` | `b787745aaaec67e7e49a80149bce967d1ddff93c` | Center print label text |
| `11be15a66049dc82b47474ea575f70ccdeba208a` | `5991fc35e60d696bb681b9418fffe1c88031481a` | Center print action content |
| `e1da91cec73744aa5427f0c7369a4fcbb1b11367` | `a85772c7465b3ad16bd5b4be434a23ff09e4e090` | Add loaded label size printer setup |
| `2ebbc3c054d64253e1e7e4d24a13861a47dc0872` | `db0988f8023d4854fcda6a0c5bdd0e0b873f4847` | Document modular label printer design |
| `2ba483dd6ed044fbcbb826914cb1db3c005de6fe` | `e174ec7112a4c113ec10c4c4d698d274edc63521` | Plan modular label printer implementation |
| `6961c6ddfbdf05fe81d8bcbc1f27fee3af3dc0ee` | `09511581b8360b9e18d0c88dc73162685f4659b8` | Add generic label printer service |
| `92a36f6fd534169c26a1c499cb5c758fd85c6bab` | `d188d8876af5091326891dc20e50b96c05212270` | Add modular Phomemo M-series drivers |
| `d25f64de0d07c2ecb1920e9e7e3613ab3d184902` | `c4f89f583f752fa99c9b7422535620c64d9afafe` | Add printer model and BLE device selection |
| `d429940890ef95ede3e3466c9c13da3b237fd086` | `a53c9b2d02b5214f399a80d6ee2579202a75c319` | Defer printer settings saves until preferences write directly |
| `046d84718faf985f9c06581ca30660d4fb1e7b65` | `e57d797cdc96d3e729e448f7ae7a5a5c867b3d83` | Verify persisted empty preference strings before reporting success |
| `02dee069253a0f19813c0b8f625be088cfee7e2e` | `1f41a74533c052cbddaa96a969e6c99bcae5d6f4` | Correct printer settings header spacing and media padding |
| `d8f1c79e0a2731991bfd91996a39be7234b478c9` | `579261411744be739ffb8458555775a394cf99f2` | Route label printing through modular drivers |
| `a6524d0f8927c0ef57210f05fe1224d0cdabc177` | `88330531186d9b71b7557dfc9978bad57e11ce80` | Correct label preview controls and status layout |
| `f028b53d964cf97f6aa0b5b3970b426a45952e7b` | `57b7fc6fa06b0f28031b4b7664489c5aa8a0510f` | Fit French preset caption within preview control |
| `b1ef836c7930f10f45d3898c7151aba0acf7bac3` | `5f28cc8d0c7d50e7e6d022935a6f33a1e7f64a4e` | Document modular Bluetooth label printers |
| `f6996b88baa080fc8ebeb5e0d2953d120e9ddd1a` | `2b4f9e6a910ca7636467ec664c42041d818b3299` | Correct label printer documentation |

## Independent review correction

The integration correction initially resurrected an unconditional Bambu tag-recording block from the historical merge. Review confirmed that upstream intentionally deferred recording until classification settled. `TagSeen::note` suppresses repeated UIDs, so the early call prevented the later Snapmaker/MIFARE classification from being recorded. Removed only that six-line block in a separate commit; preserved header updates and the later classified call. No existing NFC logging host-test pattern was present, so no new test harness was added. The reviewer reproduction and all eight Python checks, both C++ checks, `check.sh`, firmware build and whitespace check were used for verification.
