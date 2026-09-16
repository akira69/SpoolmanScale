#include "label_print_screen.h"

#include <Arduino.h>
#include <lvgl.h>

#include "services/filaman_api.h"
#include "app/app_state.h"
#include "lang.h"
#include "services/backend.h"
#include "services/prefs_store.h"
#include "services/wifi_manager.h"
#include "ui/navigation.h"
#include "ui/more_info_screen.h"
#include "ui/ui_common.h"

namespace {
constexpr size_t kPresetCapacity = 64;
static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_list = nullptr;
static lv_obj_t* s_status = nullptr;
static bool s_open_pending = false;
static bool s_fetch_pending = false;
static bool s_back_pending = false;
static int s_spool_id = 0;
static FilaManLabelPreset s_presets[kPresetCapacity];
static size_t s_count = 0;

static void fillList();

static void setStatus(const char* text) {
  if (s_status) lv_label_set_text(s_status, text);
}

static void addPresetRow(int id, const char* name) {
  lv_obj_t* row = lv_btn_create(s_list);
  lv_obj_set_width(row, 392);
  lv_obj_set_height(row, 42);
  lv_obj_set_style_bg_color(row, lv_color_hex(id == prefsGetInt("label_preset", 0) ? 0x174f46 : 0x102035), 0);
  lv_obj_set_style_radius(row, 6, 0);
  lv_obj_add_event_cb(row, [](lv_event_t* e) {
    const int id = filamanLabelPresetId(lv_event_get_user_data(e));
    prefsPutInt("label_preset", id);
    fillList();
  }, LV_EVENT_CLICKED, (void*)(intptr_t)id);
  lv_obj_t* label = lv_label_create(row);
  lv_label_set_text(label, name);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, 360);
  lv_obj_center(label);
}

static void fillList() {
  if (!s_list) return;
  lv_obj_clean(s_list);
  addPresetRow(0, T(STR_LABEL_DEFAULT));
  for (size_t i = 0; i < s_count; ++i) addPresetRow(s_presets[i].id, s_presets[i].name);
}

static void fetchPresets() {
  if (!s_screen) return;
  if (!wifiManagerIsConnected()) { setStatus(T(STR_LABEL_NO_WIFI)); return; }
  setStatus(T(STR_LABEL_LOADING));
  const int code = filamanListLabelPresets(backendBaseUrl(), filamanApiKey(),
                                           s_presets, kPresetCapacity, &s_count);
  if (code != 200) {
    s_count = 0;
    fillList();
    setStatus(code > 0 ? T(STR_LABEL_LOAD_FAIL) : T(STR_LABEL_LOAD_FAIL));
    return;
  }
  int selected = prefsGetInt("label_preset", 0);
  bool found = selected == 0;
  for (size_t i = 0; i < s_count; ++i) if (s_presets[i].id == selected) found = true;
  if (!found) {
    prefsPutInt("label_preset", 0);
    setStatus(T(STR_LABEL_PRESET_REMOVED));
  } else if (!s_count) {
    setStatus(T(STR_LABEL_NONE));
  } else {
    setStatus("");
  }
  fillList();
}

static void showScreen() {
  if (!backendIsFilaMan() || s_spool_id <= 0 || !sm_found || sm_id != s_spool_id) return;
  hideAllOverlays();
  releaseScreen(&s_screen);
  s_screen = lv_obj_create(lv_scr_act());
  lv_obj_set_size(s_screen, 480, 320);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0b1525), 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(s_screen);
  lv_label_set_text(title, T(STR_LABEL_PRESET_TITLE));
  lv_obj_set_style_text_color(title, lv_color_hex(0x28d49a), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_ext_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t* back = lv_btn_create(s_screen);
  lv_obj_set_size(back, 82, 34);
  lv_obj_set_pos(back, 12, 8);
  lv_obj_add_event_cb(back, [](lv_event_t*) { s_back_pending = true; }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* back_label = lv_label_create(back);
  lv_label_set_text(back_label, LV_SYMBOL_LEFT);
  lv_obj_center(back_label);

  lv_obj_t* refresh = lv_btn_create(s_screen);
  lv_obj_set_size(refresh, 100, 34);
  lv_obj_set_pos(refresh, 368, 8);
  lv_obj_add_event_cb(refresh, [](lv_event_t*) { s_fetch_pending = true; }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* refresh_label = lv_label_create(refresh);
  lv_label_set_text(refresh_label, T(STR_LABEL_REFRESH));
  lv_obj_center(refresh_label);

  s_status = lv_label_create(s_screen);
  lv_obj_set_width(s_status, 430);
  lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(s_status, lv_color_hex(0xb7c9dc), 0);
  lv_obj_set_pos(s_status, 25, 48);

  s_list = lv_obj_create(s_screen);
  lv_obj_set_size(s_list, 420, 220);
  lv_obj_set_pos(s_list, 30, 78);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s_list, 8, 0);
  lv_obj_set_style_pad_row(s_list, 6, 0);
  lv_obj_set_style_bg_color(s_list, lv_color_hex(0x09111e), 0);
  fillList();
  s_fetch_pending = true;
}
}  // namespace

void requestLabelPresetScreen(int spool_id) {
  s_spool_id = spool_id;
  s_open_pending = true;
}

void handleLabelPrintDeferredActions() {
  if (s_back_pending) {
    s_back_pending = false;
    releaseScreen(&s_screen);
    s_list = nullptr;
    s_status = nullptr;
    showMoreInfoScreen();
  }
  if (s_open_pending) { s_open_pending = false; showScreen(); }
  if (s_fetch_pending) { s_fetch_pending = false; fetchPresets(); }
}

void hideLabelPrintOverlays() {
  releaseScreen(&s_screen);
  s_list = nullptr;
  s_status = nullptr;
  s_fetch_pending = false;
  s_open_pending = false;
  s_back_pending = false;
}
