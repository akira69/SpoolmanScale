# Spool Search and Preset Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 10-row server-paged spool search to SpoolmanScale and synchronize one user-scoped active spool label preset between FilaMan, its browser designer, and the scale.

**Architecture:** Reuse FilaMan's existing spool search endpoint and the scale's existing HTTP/loading paths. Store preset selection as one nullable `selected_at` marker on `LabelPreset`; FilaMan owns selection truth while the scale keeps NVS as an offline and old-server fallback. Selection stays keyed by preset ID, so label renderers and preset formats remain unchanged.

**Tech Stack:** FastAPI, SQLAlchemy async ORM, Alembic, pytest, Astro/TypeScript, Vitest, ESP32 Arduino, ArduinoJson, LVGL 8.3, PlatformIO

**Spec:** `docs/superpowers/specs/2026-09-20-spool-search-preset-sync-design.md`

## Global Constraints

- SpoolmanScale worktree: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale`, branch `feat/filaman-label-integration`.
- FilaMan worktree: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api`, branch `feat/scale-label-api`.
- The touchscreen requests exactly 10 newest active spools per server page.
- Search runs only on submit; the submitted value supports FilaMan's existing text and `#ID` search.
- Clear restores the unfiltered newest-spool list at page 1.
- Every scale network load shows the existing loading overlay and balances it on success and failure.
- One selected spool preset exists per FilaMan user; no selected row means Default.
- Successful server reads override NVS; offline and old-server reads retain a still-valid NVS choice; a vanished preset becomes Default.
- Selection is by numeric preset ID and does not alter legacy, version 2, or future renderer data.
- Use existing libraries and helpers; add no dependencies or speculative abstractions.
- Follow strict RED/GREEN TDD and commit each task in its owning repository.

## Review Focus

- Search values containing `#`, `&`, spaces, UTF-8, or 200 characters must remain one encoded query value; Task 3 adds the client contract test.
- A stale NVS preset on a successful new-server list must become Default, while a failed or old-server request must not erase it; Tasks 3 and 5 add parser and integration tests.
- Another user's, non-spool, or deleted preset ID must return 404 without clearing the current selection; Task 1 adds API tests.
- Browser Load must distinguish owned, built-in, cross-entity, filament, and sheet choices; Task 2 adds DOM tests for every source.
- Search page transitions, cancellation, empty results, and HTTP failures must leave a usable screen with no stuck loading overlay; Task 4 extends the production UI shim.

---

### Task 1: FilaMan Preset Selection Persistence and API

**Files:**
- Create: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/backend/alembic/versions/add_label_preset_selection.py`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/backend/app/models/label_preset.py`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/backend/app/api/v1/label_presets.py`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/backend/app/api/v1/labels.py`
- Test: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/backend/tests/test_label_presets.py`
- Test: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/backend/tests/test_label_render.py`
- Test: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/backend/tests/test_migration_graph.py`

**Interfaces:**
- Consumes: authenticated `Principal.user_id`, existing `_lock_user_presets(db, user_id)`, and `LabelPreset.preset_type == "spool"`.
- Produces: `LabelPreset.selected_at: datetime | None`; `PUT /api/v1/me/label-presets/selection` with `{"preset_id": int | null}` and `204`; additive `selected: bool` in `GET /api/v1/labels/presets`.

- [ ] **Step 1: Write failing persistence and API tests**

Create two owned spool presets, one foreign spool preset, and one owned filament preset, then pin the public behavior with these assertions:

```python
selection_path = "/api/v1/me/label-presets/selection"

response = await client.put(
    selection_path,
    json={"preset_id": first.id},
    headers={"X-CSRF-Token": csrf_token},
)
assert response.status_code == 204
assert (await client.get("/api/v1/labels/presets")).json() == [
    {"id": first.id, "name": first.name, "selected": True},
    {"id": second.id, "name": second.name, "selected": False},
]

assert (await client.put(
    selection_path,
    json={"preset_id": foreign.id},
    headers={"X-CSRF-Token": csrf_token},
)).status_code == 404
assert (await client.put(
    selection_path,
    json={"preset_id": filament.id},
    headers={"X-CSRF-Token": csrf_token},
)).status_code == 404

after_rejections = await client.get("/api/v1/labels/presets")
assert next(item for item in after_rejections.json() if item["id"] == first.id)["selected"] is True

assert (await client.put(
    selection_path,
    json={"preset_id": None},
    headers={"X-CSRF-Token": csrf_token},
)).status_code == 204
assert all(not item["selected"] for item in (await client.get("/api/v1/labels/presets")).json())
```

Also test that a successful spool preset upsert selects the returned row, deleting that row leaves Default, a device token gets 403, and two deliberately marked rows expose only the newest marker as selected.

- [ ] **Step 2: Run the backend tests and verify RED**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/scale-label-api
uv run --project backend pytest backend/tests/test_label_presets.py backend/tests/test_label_render.py backend/tests/test_migration_graph.py -q
```

Expected: FAIL because `selected_at`, the selection endpoint, and the `selected` response member do not exist.

- [ ] **Step 3: Add the nullable selection column and migration**

Use the repository's timezone-aware type:

```python
# backend/app/models/label_preset.py
from datetime import datetime
from app.models.base import Base, TimestampMixin, TZDateTime

selected_at: Mapped[datetime | None] = mapped_column(TZDateTime(), nullable=True)
```

Create the single-head migration:

```python
"""add selected spool label preset marker"""

import sqlalchemy as sa
from alembic import op

revision = "add_label_preset_selection"
down_revision = "merge_scale_labels_freeform_20260916"
branch_labels = None
depends_on = None


def upgrade() -> None:
    with op.batch_alter_table("label_presets") as batch_op:
        batch_op.add_column(sa.Column("selected_at", sa.DateTime(timezone=True), nullable=True))


def downgrade() -> None:
    with op.batch_alter_table("label_presets") as batch_op:
        batch_op.drop_column("selected_at")
```

Update `test_migration_graph_has_one_head()` to expect `add_label_preset_selection`.

- [ ] **Step 4: Implement the minimal selection transaction and endpoint**

Keep the write helper in `label_presets.py`, beside the existing per-user lock and upsert transaction:

```python
class LabelPresetSelectionInput(BaseModel):
    preset_id: int | None = Field(default=None, ge=1)


async def _set_selected_spool_preset(
    db: DBSession,
    user_id: int,
    preset: LabelPreset | None,
) -> None:
    await db.execute(
        update(LabelPreset)
        .where(
            LabelPreset.user_id == user_id,
            LabelPreset.preset_type == "spool",
            LabelPreset.selected_at.is_not(None),
        )
        .values(selected_at=None)
    )
    if preset is not None:
        preset.selected_at = datetime.now(timezone.utc)
```

Import `timezone` and SQLAlchemy `update`. Add the endpoint under `/me/label-presets`, acquire `_lock_user_presets`, resolve a non-null ID by user and `preset_type == "spool"`, reject it before clearing on 404, call `_set_selected_spool_preset`, and return `Response(status_code=204)`. In the existing upsert transaction, call the helper after `flush()` when `preset_type == "spool"` so Save is also a selection action.

In `labels.py`, list the rows including `selected_at`, choose at most one selected ID with:

```python
marked = [row for row in rows if row.selected_at is not None]
selected_id = max(
    marked,
    key=lambda row: (row.selected_at, row.id),
).id if marked else None
return [
    {"id": row.id, "name": row.name, "selected": row.id == selected_id}
    for row in rows
]
```

Filter the `max()` input to marked rows so `None` never compares with a timestamp.

- [ ] **Step 5: Run the focused backend tests and verify GREEN**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/scale-label-api
uv run --project backend pytest backend/tests/test_label_presets.py backend/tests/test_label_render.py backend/tests/test_migration_graph.py -q
```

Expected: PASS, including 404 without selection loss, Default, deletion fallback, auth, and duplicate-marker repair.

- [ ] **Step 6: Commit the FilaMan persistence/API task**

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/scale-label-api
git add backend/alembic/versions/add_label_preset_selection.py backend/app/models/label_preset.py backend/app/api/v1/label_presets.py backend/app/api/v1/labels.py backend/tests/test_label_presets.py backend/tests/test_label_render.py backend/tests/test_migration_graph.py
git commit -m "Add shared spool label preset selection"
```

**Review focus:** Verify wrong-owner/type validation occurs before clearing, the user lock covers clear-and-set, Default is all null, and only the scale-specific list adds `selected` so existing `/me/label-presets` response clients do not break.

---

### Task 2: FilaMan Browser Selection Hook

**Files:**
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/frontend/src/lib/label-preset-storage.ts`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/frontend/src/lib/freeform-label/editor-storage.ts`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/frontend/src/lib/freeform-label/editor-page.ts`
- Test: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/frontend/src/lib/label-preset-storage.test.ts`
- Test: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/frontend/src/lib/freeform-label/editor-controller.dom.test.ts`

**Interfaces:**
- Consumes: Task 1's `PUT /api/v1/me/label-presets/selection`; existing `ApiLabelPreset.id`; preset source prefixes `own:`, `cross:`, and `builtin:`.
- Produces: optional `databaseId` in the browser cache; `selectLabelPreset(presetId: number | null): Promise<boolean>`; browser Save and Load update the shared spool selection.

- [ ] **Step 1: Write failing storage and DOM tests**

Extend storage tests to require database IDs and the selection body:

```ts
const cache = buildDesignerPresetCache([
  { id: 42, preset_type: 'spool', name: 'M220', data: { settings: {} } },
], 'spool')
expect(cache.presets[0].databaseId).toBe(42)

vi.spyOn(api, 'put').mockResolvedValue(undefined)
expect(await selectLabelPreset(42)).toBe(true)
expect(api.put).toHaveBeenCalledWith('/me/label-presets/selection', { preset_id: 42 })
expect(await selectLabelPreset(null)).toBe(true)
expect(api.put).toHaveBeenLastCalledWith('/me/label-presets/selection', { preset_id: null })
```

Add DOM cases that click Load for:

```ts
expect(selectLabelPreset).toHaveBeenCalledWith(42)   // owned spool preset
expect(selectLabelPreset).toHaveBeenCalledWith(null) // built-in spool preset
expect(selectLabelPreset).toHaveBeenCalledWith(null) // cross-entity preset
expect(selectLabelPreset).not.toHaveBeenCalled()     // filament editor
```

Also prove a successful new spool-preset Save keeps the returned database ID in the stored cache, while sheet and filament saves do not make selection requests.

- [ ] **Step 2: Run frontend tests and verify RED**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/scale-label-api
npm --prefix frontend test -- src/lib/label-preset-storage.test.ts src/lib/freeform-label/editor-controller.dom.test.ts
```

Expected: FAIL because cached presets drop database IDs and Load never calls the selection endpoint.

- [ ] **Step 3: Preserve database IDs and add the API helper**

Add the optional field in both cache-facing types and preserve only positive integer values while parsing:

```ts
export interface CachedDesignerPreset {
  databaseId?: number
  name: string
  data: LabelDesignerPresetData
  settings: unknown
}

export interface StoredPreset {
  databaseId?: number
  name: string
  data: LabelDesignerPresetData
  settings?: unknown
}
```

`buildDesignerPresetCache()` sets `databaseId: preset.id`. `readStoredPresets()` copies `databaseId` only when `Number.isInteger(databaseId) && databaseId > 0`. After the existing upsert returns, update the named item already written by the optimistic mutation:

```ts
const saved = await api.put<ApiLabelPreset>(
  `/me/label-presets/${presetType}/item`,
  buildLabelPresetUpsertBody(storageKey, preset, previousName),
)
const cache = JSON.parse(localStorage.getItem(storageKey) ?? '{}') as DesignerPresetCache
const cached = cache.presets?.find(item => item.name === preset.name)
if (cached) {
  cached.databaseId = saved.id
  safeWrite(storageKey, cache)
}
return true
```

Keep the existing catch path returning false so `persistStoredPresetMutation()` restores the previous cache on an upsert failure.

Add:

```ts
export async function selectLabelPreset(presetId: number | null): Promise<boolean> {
  try {
    await api.put('/me/label-presets/selection', { preset_id: presetId })
    return true
  } catch (error) {
    console.warn('Could not select the label preset', error)
    return false
  }
}
```

- [ ] **Step 4: Hook only spool designer Load into selection**

In `editor-page.ts`, retain the source parsed from the dropdown. After the design is loaded locally:

```ts
if (entityType === 'spool') {
  if (source === 'own' && preset.databaseId) void selectLabelPreset(preset.databaseId)
  else if (source === 'builtin' || source === 'cross') void selectLabelPreset(null)
}
```

Keep successful spool Save selected through Task 1's transactional upsert. Ensure the returned ID is written into the new browser cache field, so a later owned Load has an ID. Do not send selection writes from filament or sheet editors.

- [ ] **Step 5: Run the focused frontend tests and verify GREEN**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/scale-label-api
npm --prefix frontend test -- src/lib/label-preset-storage.test.ts src/lib/freeform-label/editor-controller.dom.test.ts
```

Expected: PASS for ID retention, owned/built-in/cross Load, Save, and non-spool isolation.

- [ ] **Step 6: Commit the FilaMan browser task**

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/scale-label-api
git add frontend/src/lib/label-preset-storage.ts frontend/src/lib/label-preset-storage.test.ts frontend/src/lib/freeform-label/editor-storage.ts frontend/src/lib/freeform-label/editor-page.ts frontend/src/lib/freeform-label/editor-controller.dom.test.ts
git commit -m "Sync browser spool label preset selection"
```

**Review focus:** Ensure database IDs are metadata only, unsupported preset data remains untouched, Load does not wait on the network, and filament/sheet choices never change the scale's spool preset.

---

### Task 3: Scale FilaMan Client and Preset Parser

**Files:**
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/src/services/filaman_label_preset_parse.h`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/src/services/filaman_api.h`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/src/services/filaman_api.cpp`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/test/test_filaman_label_presets.py`
- Create: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/test/test_filaman_spool_search.py`

**Interfaces:**
- Consumes: FilaMan's existing spool query and Task 1's additive `selected` list member plus selection endpoint.
- Produces: `FilaManLabelPreset.selected`; list-wide `selection_known`; optional `search_term` for paged spools; `filamanSelectLabelPreset(..., int preset_id, ...)`, where `0` serializes as JSON null.

- [ ] **Step 1: Write failing parser and HTTP contract checks**

Extend the existing host parser program:

```cpp
FilaManLabelPreset presets[2]{};
size_t count = 0;
bool known = false;
assert(filamanParseLabelPresets(
    "[{\"id\":7,\"name\":\"Saved\",\"selected\":true}]",
    presets, 2, &count, &known) == 0);
assert(count == 1 && known && presets[0].selected);

known = true;
assert(filamanParseLabelPresets(
    "[{\"id\":7,\"name\":\"Old server\"}]",
    presets, 2, &count, &known) == 0);
assert(!known && !presets[0].selected);
```

Reject mixed/malformed contracts such as one row missing `selected` while another supplies it, or a string value for `selected`.

Create an HTTP shim test that records URLs and bodies, then assert:

```cpp
assert(last_url == "http://fila/api/v1/spools?page=2&page_size=10&sort_by=id&sort_order=desc&search=%23PLA%20%26%20blue");
assert(last_body == "{\"preset_id\":42}");
assert(last_body == "{\"preset_id\":null}");
```

- [ ] **Step 2: Run the host checks and verify RED**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/filaman-scale
python3 test/test_filaman_label_presets.py
python3 test/test_filaman_spool_search.py
```

Expected: FAIL to compile because the new parser, search, and selection signatures do not exist.

- [ ] **Step 3: Extend the parser without changing preset payload handling**

Use an explicit field-presence scan because ArduinoJson's null checks cannot distinguish absent from present-null:

```cpp
struct FilaManLabelPreset {
  int id;
  char name[64];
  bool selected;
};

inline int filamanParseLabelPresets(
    const char* json,
    FilaManLabelPreset* out,
    size_t capacity,
    size_t* count,
    bool* selection_known) {
  if (count) *count = 0;
  if (selection_known) *selection_known = false;
  if (!json || !out || !count || !capacity) return -1;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return -2;
  JsonArrayConst presets = doc.as<JsonArrayConst>();
  if (presets.isNull() || presets.size() > capacity) return -3;
  bool any_selected_field = false;
  bool all_selected_fields = presets.size() != 0;
  size_t parsed = 0;
  for (JsonVariantConst value : presets) {
    JsonObjectConst preset = value.as<JsonObjectConst>();
    const int id = preset["id"] | -1;
    const char* name = preset["name"].as<const char*>();
    if (preset.isNull() || id <= 0 || !name || !name[0]) return -3;
    bool has_selected_field = false;
    for (JsonPairConst field : preset)
      if (strcmp(field.key().c_str(), "selected") == 0) has_selected_field = true;
    if (has_selected_field && !preset["selected"].is<bool>()) return -3;
    any_selected_field = any_selected_field || has_selected_field;
    all_selected_fields = all_selected_fields && has_selected_field;
    out[parsed].id = id;
    out[parsed].selected = has_selected_field && preset["selected"].as<bool>();
    size_t length = strlen(name);
    if (length >= sizeof(out[parsed].name)) {
      length = sizeof(out[parsed].name) - 1;
      while (length && ((unsigned char)name[length] & 0xC0) == 0x80) --length;
    }
    memcpy(out[parsed].name, name, length);
    out[parsed].name[length] = '\0';
    ++parsed;
  }
  if (any_selected_field != all_selected_fields) return -3;
  if (selection_known) *selection_known = parsed > 0 && all_selected_fields;
  *count = parsed;
  return 0;
}
```

For every nonempty response, require either all rows to contain a boolean `selected` or no rows to contain it. Initialize every output row's `selected` to false. Empty arrays return success and Default behavior.

Thread the compatibility result through the existing list client:

```cpp
int filamanListLabelPresets(
    const char* base_url, const char* api_key,
    FilaManLabelPreset* out, size_t capacity, size_t* count,
    bool* selection_known = nullptr, uint32_t timeout_ms = 8000);
```

Pass `selection_known` directly to `filamanParseLabelPresets()` after the HTTP body validation succeeds.

- [ ] **Step 4: Add search and selection requests using existing helpers**

Change the paged spool signature to:

```cpp
int filamanGetSpoolPageJson(
    const char* base_url, const char* api_key,
    int page, int page_size, JsonDocument& out_doc, int* out_total,
    const char* search_term = nullptr, uint32_t timeout_ms = 8000);
```

Build the URL with explicit ID-descending sort, and append `&search=` plus the existing `urlEncodeQuery(search_term)` only for a nonempty query.

Add:

```cpp
int filamanSelectLabelPreset(
    const char* base_url, const char* api_key,
    int preset_id, uint32_t timeout_ms = 8000);
```

Reject negative IDs, require a base URL and API key, send `Content-Type: application/json`, reuse `addApiKey()`, PUT to `/api/v1/me/label-presets/selection`, and serialize `0` as `{"preset_id":null}`.

- [ ] **Step 5: Run host checks and verify GREEN**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/filaman-scale
python3 test/test_filaman_label_presets.py
python3 test/test_filaman_spool_search.py
```

Expected: PASS, including encoded reserved characters, exact page size, numeric selection, Default, old-server parsing, and malformed response rejection.

- [ ] **Step 6: Commit the scale client task**

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/filaman-scale
git add src/services/filaman_label_preset_parse.h src/services/filaman_api.h src/services/filaman_api.cpp test/test_filaman_label_presets.py test/test_filaman_spool_search.py
git commit -m "Add searchable spools and preset selection client"
```

**Review focus:** Confirm the existing encoder is reused, search is never concatenated raw, 0 and negative IDs have different meanings, and an old response cannot masquerade as server Default.

---

### Task 4: Scale Spool Search Touchscreen

**Files:**
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/src/ui/manual_spool_screen.cpp`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/src/lang.h`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/src/lang.cpp`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/test/test_manual_spool_loading.py`

**Interfaces:**
- Consumes: Task 3's `filamanGetSpoolPageJson(..., search_term)`; existing overlay, style, navigation, and LVGL keyboard APIs.
- Produces: fixed 10-row paging, submitted search buffer, separate keyboard screen, Clear, and query-preserving Previous/Next.

- [ ] **Step 1: Extend the production UI shim with failing behavior checks**

Add LVGL textarea/keyboard shims and record every page, size, and search argument. Exercise:

```python
assert requested_page_sizes == [10]
assert requested_searches == [""]

tap_search()
enter_search("  #123 & blue  ")
submit_keyboard()
handleManualSpoolDeferredActions()
assert requested_pages[-1] == 1
assert requested_page_sizes[-1] == 10
assert requested_searches[-1] == "#123 & blue"

tap_next()
handleManualSpoolDeferredActions()
assert requested_pages[-1] == 2
assert requested_searches[-1] == "#123 & blue"

tap_clear()
handleManualSpoolDeferredActions()
assert requested_pages[-1] == 1
assert requested_searches[-1] == ""
assert loading_shown == loading_hidden
```

Also cover keyboard cancel, empty submit as Clear, zero-result copy, HTTP failure, and `hideManualSpoolOverlays()` clearing the query and both screens.

- [ ] **Step 2: Run the UI shim and verify RED**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/filaman-scale
python3 test/test_manual_spool_loading.py
```

Expected: FAIL because the current screen requests up to 20 and has no search flow.

- [ ] **Step 3: Add the minimal fixed state and deferred transitions**

Use fixed state in the existing screen module:

```cpp
constexpr int kSpoolsPerPage = 10;
constexpr size_t kSearchCapacity = 801;  // 200 four-byte UTF-8 characters plus NUL
char search_term[kSearchCapacity] = "";
lv_obj_t* search_screen = nullptr;
lv_obj_t* search_input = nullptr;
bool search_open_pending = false;
bool search_submit_pending = false;
bool search_cancel_pending = false;
bool search_clear_pending = false;

int rowsPerPage() { return kSpoolsPerPage; }
```

LVGL callbacks only copy/trim the textarea into the fixed buffer and set pending flags. `handleManualSpoolDeferredActions()` owns screen replacement and network work. Previous and Next change only `page`; submit and Clear set `page = 1`.

- [ ] **Step 4: Build the list and keyboard layouts with existing styles**

Add `STR_SPOOLS_SEARCH`, `STR_SPOOLS_CLEAR`, `STR_SPOOLS_SEARCH_HINT`, and `STR_SPOOLS_SEARCH_EMPTY` in German, English, and French. Use `LV_SYMBOL_SEARCH`, `styleOutlineButton`, `styleListPanel`, `buildSubHeader`, and the existing loading overlay. Do not add a custom keyboard map or new UI component.

Call:

```cpp
code = filamanGetSpoolPageJson(
    backendBaseUrl(), filamanApiKey(), page, rowsPerPage(),
    doc, &total, search_term[0] ? search_term : nullptr);
```

Use search-specific empty text only when `search_term[0] != '\0'`. Clear stale rows before each request and leave the screen navigable after an error.

- [ ] **Step 5: Run the UI shim and repository convention gate**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/filaman-scale
python3 test/test_manual_spool_loading.py
scripts/check.sh
```

Expected: PASS with 10-row requests, retained search while paging, working Clear/cancel, and balanced overlays.

- [ ] **Step 6: Commit the touchscreen search task**

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/filaman-scale
git add src/ui/manual_spool_screen.cpp src/lang.h src/lang.cpp test/test_manual_spool_loading.py
git commit -m "Add touchscreen spool search"
```

**Review focus:** Check touch targets on 480 by 320, keyboard submit/cancel behavior, UTF-8 byte bounds, no HTTP work in callbacks, and fixed state reset when leaving the flow.

---

### Task 5: Scale Preset Sync and Preview Integration

**Files:**
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/src/ui/label_preset_selection.cpp`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/src/ui/label_preset_selection.h`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/src/ui/label_print_screen.cpp`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/test/test_filaman_label_presets.py`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/test/test_filaman_print_flow.py`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/test/test_filaman_print_deferred.py`

**Interfaces:**
- Consumes: Task 3's selected preset parser/list result and `filamanSelectLabelPreset()`; NVS key `label_preset`; existing preview request flow.
- Produces: deferred server-first row selection; server-to-NVS synchronization on preset-list open and before preview; offline/old-server fallback.

- [ ] **Step 1: Add failing synchronization and deferred-selection checks**

Pin the pure decision rules in the host test with these assertions:

```cpp
assert(filamanResolvedPresetId(presets, 2, true, 7) == 42);  // server selected
assert(filamanResolvedPresetId(presets, 2, true, 99) == 0); // server Default
assert(filamanResolvedPresetId(presets, 2, false, 7) == 7); // old server, valid NVS
assert(filamanResolvedPresetId(presets, 2, false, 99) == 0); // vanished NVS
```

Extend the flow/deferred shims to prove:

```cpp
tapPreset(42);
assert(nvs_preset == 7);                 // callback did not write
handleLabelPrintDeferredActions();
assert(selection_requests == 1);
assert(nvs_preset == 42);                // written after HTTP 204

selection_http_status = 500;
tapPreset(7);
handleLabelPrintDeferredActions();
assert(nvs_preset == 42);                // failed write retained fallback

requestLabelPreviewScreen(123);
handleLabelPrintDeferredActions();
assert(list_requests_happened_before_preview_render);
assert(loading_shown == loading_hidden);
```

Also cover offline preview fallback, old-server list fallback, successful Default, and a selected preset deleted between requests.

- [ ] **Step 2: Run scale preset/print tests and verify RED**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/filaman-scale
python3 test/test_filaman_label_presets.py
python3 test/test_filaman_print_flow.py
python3 test/test_filaman_print_deferred.py
```

Expected: FAIL because callbacks write NVS immediately and preview does not synchronize from the server.

- [ ] **Step 3: Add one deterministic resolver and deferred selection state**

Keep the resolver beside the preset parser or selection callback, with no new service class:

```cpp
inline int filamanResolvedPresetId(
    const FilaManLabelPreset* presets,
    size_t count,
    bool selection_known,
    int local_id) {
  if (selection_known) {
    for (size_t i = 0; i < count; ++i)
      if (presets[i].selected) return presets[i].id;
    return 0;
  }
  if (local_id == 0) return 0;
  for (size_t i = 0; i < count; ++i)
    if (presets[i].id == local_id) return local_id;
  return 0;
}
```

Change `labelPresetRowCb()` to queue the numeric ID through `requestLabelPresetSelection(int preset_id)` instead of writing preferences. Use `-1` as the no-request sentinel because `0` is Default.

- [ ] **Step 4: Synchronize before list drawing and preview rendering**

Factor one local `fetchAndSyncPresets(bool keep_rows)` function inside `label_print_screen.cpp`. It must:

1. leave NVS unchanged and return false while offline or on a failed HTTP response;
2. show the existing loading overlay and `HttpStall` for the GET;
3. parse `selection_known`, resolve the ID, and write NVS only when the successful response changes it;
4. keep rows for the preset screen or discard them after preview synchronization.

Call it when opening the preset list and immediately before the first preview render. Continue preview with NVS when it returns false.

For queued selection, show the loading overlay, PUT the server selection, hide the overlay on every path, and only then call `prefsPutInt()` and refresh rows. Treat HTTP 204 as success; preserve the prior value otherwise.

- [ ] **Step 5: Run focused tests and build the firmware**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/filaman-scale
python3 test/test_filaman_label_presets.py
python3 test/test_filaman_print_flow.py
python3 test/test_filaman_print_deferred.py
pio run -e wt32-sc01-plus
scripts/check.sh
```

Expected: PASS; the binary remains below the 3,145,728-byte OTA application slot checked by `scripts/check.sh`.

- [ ] **Step 6: Commit the scale synchronization task**

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/filaman-scale
git add src/ui/label_preset_selection.cpp src/ui/label_preset_selection.h src/ui/label_print_screen.cpp test/test_filaman_label_presets.py test/test_filaman_print_flow.py test/test_filaman_print_deferred.py
git commit -m "Sync active FilaMan label preset"
```

**Review focus:** Confirm every request is deferred, list failures never erase NVS, successful new-server Default does erase stale NVS, selection failure does not repaint a false choice, and preview waits for synchronization only while online.

---

### Task 6: Documentation and Cross-Repository Verification

**Files:**
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/scale-label-api/docs/scale-label-api.md`
- Modify: `/Users/dfinch/Code/filaman-system/worktrees/filaman-scale/README.md`

**Interfaces:**
- Consumes: Tasks 1 through 5 complete contracts.
- Produces: public API examples, touchscreen behavior documentation, complete test evidence, and clean commits in both branches.

- [ ] **Step 1: Document the exact public contracts**

Add these examples to FilaMan's scale API document:

```http
GET /api/v1/labels/presets
PUT /api/v1/me/label-presets/selection
Content-Type: application/json

{"preset_id": 42}
```

Document null as Default, user API key ownership, 403 for principals without a user, 404 for wrong owner/type/missing ID, and the additive `selected` boolean. State that preset content/version is unchanged.

Update the scale README flow to say the spool list loads 10 newest active spools per page, Search accepts text or `#ID`, Clear restores the newest list, and preset selection follows the configured FilaMan API-key user with NVS fallback.

- [ ] **Step 2: Run the complete relevant FilaMan verification**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/scale-label-api
uv run --project backend pytest backend/tests/test_label_presets.py backend/tests/test_label_render.py backend/tests/test_migration_graph.py -q
npm --prefix frontend test -- src/lib/label-preset-storage.test.ts src/lib/freeform-label/editor-controller.dom.test.ts
npm --prefix frontend run check
```

Expected: all commands PASS with no type error.

- [ ] **Step 3: Run the complete relevant scale verification**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/filaman-scale
python3 test/test_filaman_label_presets.py
python3 test/test_filaman_spool_search.py
python3 test/test_manual_spool_loading.py
python3 test/test_filaman_print_flow.py
python3 test/test_filaman_print_deferred.py
pio run -e wt32-sc01-plus
scripts/check.sh
```

Expected: all commands PASS, convention checks pass, and firmware fits the OTA slot.

- [ ] **Step 4: Commit documentation in each owning repository**

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/scale-label-api
git add docs/scale-label-api.md
git commit -m "Document shared label preset selection"

cd /Users/dfinch/Code/filaman-system/worktrees/filaman-scale
git add README.md
git commit -m "Document spool search and preset sync"
```

- [ ] **Step 5: Review both final diffs**

Run:

```bash
cd /Users/dfinch/Code/filaman-system/worktrees/scale-label-api
git status --short
git diff upstream/devel...HEAD --check

cd /Users/dfinch/Code/filaman-system/worktrees/filaman-scale
git status --short
git diff upstream/dev...HEAD --check
```

Expected: both worktrees are clean and both `--check` commands print nothing.

**Review focus:** Compare documentation examples against the implemented paths and status codes, confirm both branches are clean, and reject any renderer, dependency, all-spool download, or per-keystroke request that entered the diff.
