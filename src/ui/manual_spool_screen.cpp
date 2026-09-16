#include "manual_spool_screen.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "app/app_state.h"
#include "app/app_loop.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/list_limits.h"
#include "services/backend.h"
#include "services/filaman_api.h"
#include "services/http_progress.h"
#include "services/wifi_manager.h"
#include "ui/more_info_screen.h"
#include "ui/navigation.h"
#include "ui/spoolman_lookup.h"
#include "ui/tag_display.h"
#include "ui/ui_common.h"

namespace {
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
  }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override {
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
    return q ? q : realloc(p, n);
  }
};

lv_obj_t* screen = nullptr;
lv_obj_t* list = nullptr;
lv_obj_t* status = nullptr;
bool open_pending = false;
bool fetch_pending = false;
bool back_pending = false;
int selected_id = 0;
int page = 1;
int total = 0;
bool page_complete = false;

int rowsPerPage() { return spool_list_limit < 20 ? spool_list_limit : 20; }

void setStatus(const char* message) {
  if (status) lv_label_set_text(status, message);
}

void buildScreen() {
  if (!backendIsFilaMan()) return;
  hideAllOverlays();
  screen = buildOverlayScreen();
  buildSubHeader(screen, T(STR_SPOOLS_TITLE), [](lv_event_t*) { back_pending = true; });

  lv_obj_t* prev = lv_btn_create(screen);
  lv_obj_set_size(prev, 60, 32);
  lv_obj_set_pos(prev, 10, 54);
  lv_obj_add_event_cb(prev, [](lv_event_t*) {
    if (page > 1) { --page; fetch_pending = true; }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* prev_text = lv_label_create(prev);
  lv_label_set_text(prev_text, LV_SYMBOL_LEFT);
  lv_obj_center(prev_text);

  lv_obj_t* next = lv_btn_create(screen);
  lv_obj_set_size(next, 60, 32);
  lv_obj_set_pos(next, 410, 54);
  lv_obj_add_event_cb(next, [](lv_event_t*) {
    if (page_complete && page * rowsPerPage() < total) { ++page; fetch_pending = true; }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* next_text = lv_label_create(next);
  lv_label_set_text(next_text, LV_SYMBOL_RIGHT);
  lv_obj_center(next_text);

  status = lv_label_create(screen);
  lv_obj_set_width(status, 320);
  lv_obj_set_pos(status, 80, 62);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(status, lv_color_hex(0xb7c9dc), 0);
  lv_label_set_text(status, "");

  list = lv_obj_create(screen);
  lv_obj_set_size(list, 460, 215);
  lv_obj_set_pos(list, 10, 95);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_style_pad_all(list, 4, 0);
  lv_obj_set_style_pad_row(list, 4, 0);
  lv_obj_set_style_bg_color(list, lv_color_hex(0x0a1020), 0);
  page = 1;
  fetch_pending = true;
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void fetchSpools() {
  if (!screen || !list || !backendIsFilaMan()) return;
  lv_obj_clean(list);
  page_complete = false;
  if (!wifiManagerIsConnected()) { setStatus(T(STR_LABEL_NO_WIFI)); return; }
  setStatus(T(STR_SPOOLS_LOADING));

  SpiRamAllocator alloc;
  JsonDocument doc(&alloc);
  HttpStallTime stall;
  const int code = filamanGetSpoolPageJson(backendBaseUrl(), filamanApiKey(),
                                            page, rowsPerPage(), doc, &total);
  if (code != 200) {
    char message[48];
    snprintf(message, sizeof(message), T(STR_SPOOLS_LOAD_FAIL), code);
    setStatus(message);
    return;
  }

  JsonArrayConst spools = doc.as<JsonArrayConst>();
  int shown = 0;
  for (JsonObjectConst spool : spools) {
    if (!lvPoolHasRoomForRow()) break;
    int id = spool["id"] | 0;
    if (id <= 0) continue;
    const char* material = spool["filament"]["material"] | "";
    const char* name = spool["filament"]["name"] | "";
    lv_obj_t* row = lv_btn_create(list);
    lv_obj_set_size(row, 442, 42);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x102035), 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      selected_id = (int)(intptr_t)lv_event_get_user_data(e);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)id);
    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text_fmt(label, "#%d  %s  %s", id, material, name);
    lv_obj_set_width(label, 420);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);
    ++shown;
  }
  page_complete = shown == (int)spools.size();
  if (!page_complete) setStatus(T(STR_SPOOLS_LOW_MEM));
  else if (!shown) setStatus(T(STR_SPOOLS_EMPTY));
  else {
    char message[56];
    snprintf(message, sizeof(message), T(STR_SPOOLS_PAGE_FMT),
             page, (total + rowsPerPage() - 1) / rowsPerPage(), total);
    setStatus(message);
  }
}
}  // namespace

void requestManualSpoolScreen() { open_pending = true; }

void handleManualSpoolDeferredActions() {
  if (back_pending) { back_pending = false; showMainScreen(); return; }
  if (open_pending) { open_pending = false; buildScreen(); }
  if (fetch_pending) { fetch_pending = false; fetchSpools(); }
  if (selected_id && screen) {
    const int id = selected_id;
    selected_id = 0;
    if (tag_present) { setStatus(T(STR_SPOOLS_REMOVE_TAG)); return; }
    setStatus(T(STR_SPOOLS_OPENING));
    clearTagDisplay();
    if (querySpoolmanById(id) && sm_found && sm_id == id) {
      cancelPendingNfcClear();
      // This runs from the app loop, outside LVGL's event callback. Free the
      // list now so More info can use its LVGL pool memory immediately.
      lv_obj_del(screen);
      screen = nullptr;
      hideManualSpoolOverlays();
      showMoreInfoScreen();
    } else setStatus(T(STR_SPOOLS_OPEN_FAIL));
  }
}

void hideManualSpoolOverlays() {
  releaseScreen(&screen);
  list = nullptr;
  status = nullptr;
  open_pending = fetch_pending = back_pending = false;
  selected_id = 0;
  page = 1;
  total = 0;
  page_complete = false;
}
