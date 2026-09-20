# Spool Search and Preset Sync Design

## Context

SpoolmanScale currently fetches active FilaMan spools one server page at a time, but it displays up to 20 rows and has no search control. Large inventories therefore require repeated paging even though FilaMan already supports server-side search by spool ID and descriptive fields.

The scale also stores its label preset choice only in ESP32 NVS. FilaMan has per-user label presets, but it does not store which spool preset that user most recently selected. The browser designer and the scale can consequently show different choices.

This work spans two existing branches:

- SpoolmanScale: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale`, branch `feat/filaman-label-integration`
- FilaMan: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api`, branch `feat/scale-label-api`

## Goals

1. Show exactly 10 newest active spools per touchscreen page.
2. Search the existing FilaMan spool endpoint by a submitted text or `#ID` query without downloading the inventory.
3. Keep search results server paginated at 10 rows per page.
4. Show a loading indication for every scale spool, preset, selection, and preview network request.
5. Store one selected spool label preset per FilaMan user, with no selected row meaning Default.
6. Let both the browser designer and the scale update that selection.
7. Let the scale adopt the server selection before it renders a preview and whenever it opens the preset list.
8. Preserve the scale's NVS preset as an offline fallback and as compatibility for older FilaMan servers.
9. Keep legacy settings presets, current version 2 presets, and future renderable preset formats synchronized by numeric preset ID without changing any renderer.

## Non-goals

- Client-side spool filtering or downloading all spools.
- Search on every keystroke.
- Alphabetic grouping for spools.
- A new label preset format or renderer changes.
- A separate preferences table, selection history, multiple named defaults, or per-printer preset choices.
- New dependencies.

## Existing behavior to reuse

- `GET /api/v1/spools` already accepts `page`, `page_size`, `search`, `sort_by`, and `sort_order`. Its search covers exact spool IDs with or without `#`, filament designation, material, color, manufacturer, lot, RFID, and searchable custom fields.
- `filamanGetSpoolPageJson()` already unwraps FilaMan's paginated spool response for the touchscreen.
- `urlEncodeQuery()` already percent-encodes query values in `src/services/filaman_api.cpp`.
- `loadingOverlayShow()`, `HttpStall`, and `loadingOverlayHide()` are the established embedded network feedback path.
- The Wi-Fi setup screen already demonstrates the LVGL textarea and keyboard pattern.
- `GET /api/v1/labels/presets` already provides the user-scoped spool preset list to the scale.
- The scale already stores `label_preset` in NVS and uses `0` for Default.
- FilaMan's freeform editor cache already normalizes legacy preset data and version 2 data before presenting them to the editor.

## Spool search design

### API request

Extend the scale client only. FilaMan's existing spool API needs no change.

```cpp
int filamanGetSpoolPageJson(
    const char* base_url,
    const char* api_key,
    int page,
    int page_size,
    JsonDocument& out_doc,
    int* out_total,
    const char* search_term = nullptr,
    uint32_t timeout_ms = 8000);
```

The request is:

```text
GET /api/v1/spools?page={page}&page_size=10&sort_by=id&sort_order=desc
GET /api/v1/spools?page={page}&page_size=10&sort_by=id&sort_order=desc&search={percent-encoded-query}
```

FilaMan defaults to active spools because `include_archived` remains absent. Explicit ID-descending sorting makes the unfiltered list newest first.

### Touchscreen flow

The spool screen has a large Search action. Tapping it opens a separate full-screen search view with:

- one single-line textarea, maximum 200 characters to match the server limit;
- the standard LVGL text keyboard;
- a visible submit action;
- a visible cancel/back action.

Submitting trims leading and trailing ASCII whitespace. A blank submission behaves like Clear. A nonblank submission stores the query in the spool screen's fixed buffer, resets the page to 1, returns to the list, and performs one server request.

When a query is active, the list screen shows Clear. Clear empties the query, resets to page 1, and reloads the newest active spools. Previous and Next keep the current query and request the adjacent server page.

The list page size is a fixed 10. It does not depend on the general `spool_list_limit`, because this screen has a known 480 by 320 layout and the server is already paging the data.

Every request clears stale rows, shows the existing loading overlay, attaches `HttpStall` progress, and hides the overlay on every return path. Empty search results use search-specific copy so users can distinguish them from an empty inventory.

## Preset selection persistence

### Data model

Add one nullable timestamp to `label_presets`:

```python
selected_at: Mapped[datetime | None] = mapped_column(TZDateTime(), nullable=True)
```

Only spool presets use this field. All null values for a user mean Default. Selection writes run while holding the existing per-user row lock, clear `selected_at` on that user's spool presets, then set the chosen preset to the current UTC time. This keeps one selected spool preset per user through the application write path without a database-specific partial index.

A timestamp is used instead of `updated_at`: editing a preset must not accidentally select it unless the successful spool-preset upsert explicitly performs the selection write. If unexpected duplicate markers exist, list responses choose the newest `selected_at`, breaking an exact tie by highest preset ID, while the next selection write repairs the rows.

Deleting the selected preset naturally returns the user to Default because no row remains selected.

### Selection API

Add a user-scoped endpoint beside the existing browser preset endpoints:

```http
PUT /api/v1/me/label-presets/selection
Content-Type: application/json

{"preset_id": 42}
```

Default is explicit null:

```json
{"preset_id": null}
```

Success returns `204 No Content`. A non-null ID must identify a spool preset owned by the authenticated user. A missing preset, another user's preset, or a filament/sheet preset returns `404`. A principal without a user ID returns `403`. Session requests retain normal CSRF enforcement; user API keys work without a browser CSRF token.

The existing scale list response becomes additive:

```json
[
  {"id": 7, "name": "Compact", "selected": false},
  {"id": 42, "name": "M220 40x30", "selected": true}
]
```

When Default is selected, every returned row has `selected: false`. The array shape and existing `id` and `name` fields remain unchanged.

### Browser designer writes

The browser cache retains each database preset's numeric ID as optional metadata:

```ts
interface StoredPreset {
  databaseId?: number
  name: string
  data: LabelDesignerPresetData
  settings?: unknown
}
```

This metadata does not affect preset rendering or migration.

- A successful spool preset save selects that returned preset ID.
- Loading an owned spool preset selects its `databaseId`.
- Loading a built-in preset or a cross-entity preset selects Default by sending null.
- Filament and sheet editors do not change the spool preset selection.
- A newly saved preset keeps the returned ID in the browser cache so a later Load can select it without refetching.

The current designer and future designers use the same storage/API layer, so no renderer-specific selection contract is introduced.

## Scale preset synchronization

Extend the scale preset parser with an additive `selected` value and a list-wide compatibility flag:

```cpp
struct FilaManLabelPreset {
  int id;
  char name[64];
  bool selected;
};

int filamanParseLabelPresets(
    const char* json,
    FilaManLabelPreset* out,
    size_t capacity,
    size_t* count,
    bool* selection_known);
```

`selection_known` is true when a nonempty response contains the `selected` member on every row. A new server with zero presets and an old server with zero presets both resolve to Default, so the empty-array ambiguity has no user-visible effect.

After a successful list request:

1. If the server supplies the selection contract, write the selected preset ID to NVS, or `0` when every row is false.
2. If the server omits the contract, retain the NVS ID when it still exists in the returned list.
3. If the NVS ID no longer exists after a successful list request, store `0`.
4. If the request fails or Wi-Fi is offline, leave NVS unchanged.

Selecting a row on the scale is deferred out of the LVGL event callback. The scale shows the loading overlay, calls the selection endpoint, and writes NVS only after `204`. On failure, it retains the previous NVS value and shows the existing HTTP error pattern.

Before opening a preview, the scale fetches the preset list once and applies the same synchronization rules. If that fetch fails, preview continues with the NVS fallback. Opening the preset list performs the same synchronization before drawing its selected row.

## Compatibility and failure behavior

- Older FilaMan servers omit `selected`; the scale keeps its valid NVS selection and still renders as it does today.
- A successful response that no longer contains the stored preset resets NVS to Default.
- A failed list or selection request never erases the last known NVS choice.
- A device token remains unable to list or select user presets. The configured user API key continues to identify the chosen FilaMan user.
- Selection is by preset ID only. Legacy settings, version 2 designs, and later server-renderable formats need no selection-specific changes.
- Search state uses an 801-byte fixed buffer, enough for 200 four-byte UTF-8 characters plus the terminator. It adds no unbounded allocations and no new dependency.

## Verification

### FilaMan

- Migration graph has one head and upgrades/downgrades the nullable column.
- API tests cover Default, owned spool preset, wrong owner, wrong type, device-token rejection, deletion fallback, and duplicate-marker repair.
- Frontend tests cover ID retention, save selection, owned Load, built-in/cross Load to Default, and no selection writes from filament/sheet editors.

### SpoolmanScale

- Parser tests cover new responses, old responses, Default, malformed selection values, and selected preset removal.
- API client tests inspect percent encoding, fixed paging parameters, and null/numeric selection bodies.
- The production UI shim covers exactly 10 rows, search submit, Clear, page retention, cancel, empty results, and balanced loading overlays.
- The firmware builds and the repository convention gate passes.
