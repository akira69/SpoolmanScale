#include "manual_spool_screen.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "app/app_state.h"
#include "app/app_loop.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/backend.h"
#include "services/filaman_api.h"
#include "services/http_progress.h"
#include "services/wifi_manager.h"
#include "ui/more_info_screen.h"
#include "ui/header_status.h"
#include "ui/loading_overlay.h"
#include "ui/navigation.h"
#include "ui/spoolman_lookup.h"
#include "ui/tag_display.h"
#include "ui/theme.h"
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
lv_obj_t* clear_button = nullptr;
lv_obj_t* search_screen = nullptr;
lv_obj_t* search_input = nullptr;
bool open_pending = false;
bool fetch_pending = false;
bool back_pending = false;
bool search_open_pending = false;
bool search_submit_pending = false;
bool search_cancel_pending = false;
bool search_clear_pending = false;
int selected_id = 0;
int page = 1;
int total = 0;
bool page_complete = false;
constexpr int kSpoolsPerPage = 10;
constexpr size_t kSearchCapacity = 801;
char search_term[kSearchCapacity] = "";

int rowsPerPage() { return kSpoolsPerPage; }

bool asciiWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

void copyTrimmedSearch() {
  const char* text = search_input ? lv_textarea_get_text(search_input) : "";
  const char* begin = text ? text : "";
  while (*begin && asciiWhitespace(*begin)) ++begin;
  const char* end = begin + strlen(begin);
  while (end > begin && asciiWhitespace(end[-1])) --end;
  size_t length = (size_t)(end - begin);
  if (length >= kSearchCapacity) length = kSearchCapacity - 1;
  memcpy(search_term, begin, length);
  search_term[length] = '\0';
}

void updateSearchControls() {
  if (!clear_button) return;
  if (search_term[0]) lv_obj_clear_flag(clear_button, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(clear_button, LV_OBJ_FLAG_HIDDEN);
}

void setStatus(const char* message) {
  if (status) lv_label_set_text(status, message);
}

void buildScreen() {
  if (!backendIsFilaMan()) return;
  hideAllOverlays();
  screen = buildOverlayScreen();
  buildSubHeader(screen, T(STR_SPOOLS_TITLE), [](lv_event_t*) { back_pending = true; });

  lv_obj_t* prev = lv_btn_create(screen);
  lv_obj_set_size(prev, 50, 44);
  lv_obj_set_pos(prev, 10, 52);
  styleOutlineButton(prev);
  lv_obj_add_event_cb(prev, [](lv_event_t*) {
    if (page > 1) { --page; fetch_pending = true; }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* prev_text = lv_label_create(prev);
  lv_label_set_text(prev_text, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(prev_text, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_center(prev_text);

  lv_obj_t* search = lv_btn_create(screen);
  lv_obj_set_size(search, 200, 44);
  lv_obj_set_pos(search, 80, 52);
  styleOutlineButton(search);
  lv_obj_add_event_cb(search, [](lv_event_t*) { search_open_pending = true; },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t* search_text = lv_label_create(search);
  char search_label[48];
  snprintf(search_label, sizeof(search_label), LV_SYMBOL_LIST "  %s", T(STR_SPOOLS_SEARCH));
  lv_label_set_text(search_text, search_label);
  lv_obj_set_style_text_color(search_text, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_center(search_text);

  clear_button = lv_btn_create(screen);
  lv_obj_set_size(clear_button, 120, 44);
  lv_obj_set_pos(clear_button, 290, 52);
  styleOutlineButton(clear_button);
  lv_obj_add_event_cb(clear_button, [](lv_event_t*) { search_clear_pending = true; },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t* clear_text = lv_label_create(clear_button);
  lv_label_set_text(clear_text, T(STR_SPOOLS_CLEAR));
  lv_obj_set_style_text_color(clear_text, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_center(clear_text);
  updateSearchControls();

  lv_obj_t* next = lv_btn_create(screen);
  lv_obj_set_size(next, 50, 44);
  lv_obj_set_pos(next, 420, 52);
  styleOutlineButton(next);
  lv_obj_add_event_cb(next, [](lv_event_t*) {
    if (page_complete && page * rowsPerPage() < total) { ++page; fetch_pending = true; }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* next_text = lv_label_create(next);
  lv_label_set_text(next_text, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(next_text, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_center(next_text);

  status = lv_label_create(screen);
  lv_obj_set_width(status, 320);
  lv_obj_set_pos(status, 80, 290);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(status, lv_color_hex(UI_COL_INK_2), 0);
  lv_label_set_text(status, "");

  list = lv_obj_create(screen);
  lv_obj_set_size(list, 460, 180);
  lv_obj_set_pos(list, 10, 100);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_style_pad_all(list, 4, 0);
  lv_obj_set_style_pad_row(list, 4, 0);
  styleListPanel(list);
  page = 1;
  fetch_pending = true;
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void buildSearchScreen() {
  releaseScreen(&search_screen);
  search_input = nullptr;
  search_screen = buildOverlayScreen();
  buildSubHeader(search_screen, T(STR_SPOOLS_SEARCH),
                 [](lv_event_t*) { search_cancel_pending = true; });

  search_input = lv_textarea_create(search_screen);
  lv_textarea_set_one_line(search_input, true);
  lv_textarea_set_placeholder_text(search_input, T(STR_SPOOLS_SEARCH_HINT));
  lv_textarea_set_max_length(search_input, 200);
  lv_textarea_set_text(search_input, search_term);
  lv_obj_set_size(search_input, 460, 44);
  lv_obj_set_pos(search_input, 10, 56);
  styleListPanel(search_input);

  lv_obj_t* submit = lv_btn_create(search_screen);
  lv_obj_set_size(submit, 210, 44);
  lv_obj_set_pos(submit, 260, 106);
  styleOutlineButton(submit);
  lv_obj_add_event_cb(submit, [](lv_event_t*) {
    copyTrimmedSearch();
    search_submit_pending = true;
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* submit_text = lv_label_create(submit);
  lv_label_set_text(submit_text, T(STR_SPOOLS_SEARCH));
  lv_obj_set_style_text_color(submit_text, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_center(submit_text);

  lv_obj_t* keyboard = lv_keyboard_create(search_screen);
  lv_keyboard_set_textarea(keyboard, search_input);
  lv_obj_set_size(keyboard, 480, 160);
  lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(keyboard, [](lv_event_t*) {
    copyTrimmedSearch();
    search_submit_pending = true;
  }, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(keyboard, [](lv_event_t*) { search_cancel_pending = true; },
                      LV_EVENT_CANCEL, nullptr);
  lv_obj_clear_flag(search_screen, LV_OBJ_FLAG_HIDDEN);
}

void fetchSpools() {
  if (!screen || !list || !backendIsFilaMan()) return;
  lv_obj_clean(list);
  page_complete = false;
  if (!wifiManagerIsConnected()) { setStatus(T(STR_LABEL_NO_WIFI)); return; }
  loadingOverlayShow(T(STR_SPOOLS_LOADING));

  SpiRamAllocator alloc;
  JsonDocument doc(&alloc);
  int code;
  {
    HttpStall stall(loadingOverlayProgress);
    code = filamanGetSpoolPageJson(backendBaseUrl(), filamanApiKey(),
                                  page, rowsPerPage(), doc, &total,
                                  search_term[0] ? search_term : nullptr);
  }
  loadingOverlayHide();
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
    styleListRow(row);
    lv_obj_add_event_cb(row, [](lv_event_t* e) {
      selected_id = (int)(intptr_t)lv_event_get_user_data(e);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)id);
    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text_fmt(label, "#%d  %s  %s", id, material, name);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COL_INK_2), 0);
    lv_obj_set_width(label, 420);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);
    ++shown;
  }
  page_complete = shown == (int)spools.size();
  if (!page_complete) setStatus(T(STR_SPOOLS_LOW_MEM));
  else if (!shown) setStatus(T(search_term[0] ? STR_SPOOLS_SEARCH_EMPTY : STR_SPOOLS_EMPTY));
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
  if (search_open_pending) { search_open_pending = false; buildSearchScreen(); }
  if (search_cancel_pending) {
    search_cancel_pending = false;
    releaseScreen(&search_screen);
    search_input = nullptr;
  }
  if (search_submit_pending) {
    search_submit_pending = false;
    releaseScreen(&search_screen);
    search_input = nullptr;
    page = 1;
    updateSearchControls();
    fetch_pending = true;
  }
  if (search_clear_pending) {
    search_clear_pending = false;
    search_term[0] = '\0';
    page = 1;
    updateSearchControls();
    fetch_pending = true;
  }
  if (fetch_pending) { fetch_pending = false; fetchSpools(); }
  if (selected_id && screen) {
    const int id = selected_id;
    selected_id = 0;
    if (tag_present) { setStatus(T(STR_SPOOLS_REMOVE_TAG)); return; }
    if (!wifiManagerIsConnected()) { setStatus(T(STR_LABEL_NO_WIFI)); return; }
    loadingOverlayShow(T(STR_SPOOLS_OPENING));
    cancelRemoteTaglessAdoption();
    clearTagDisplay();
    bool found;
    {
      HttpStall stall(loadingOverlayProgress);
      found = querySpoolmanById(id) && sm_found && sm_id == id;
    }
    loadingOverlayHide();
    if (found) {
      cancelPendingNfcClear();
      updateHeaderStatus();
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
  releaseScreen(&search_screen);
  list = nullptr;
  status = nullptr;
  clear_button = nullptr;
  search_input = nullptr;
  open_pending = fetch_pending = back_pending = false;
  search_open_pending = search_submit_pending = false;
  search_cancel_pending = search_clear_pending = false;
  search_term[0] = '\0';
  selected_id = 0;
  page = 1;
  total = 0;
  page_complete = false;
}
