#include "label_print_screen.h"

#include <Arduino.h>
#include <lvgl.h>

#include "services/filaman_api.h"
#include "services/filaman_labels.h"
#include "services/phomemo_m220.h"
#include "services/filaman_print_pending.h"
#include "services/http_progress.h"
#include "app/app_state.h"
#include "lang.h"
#include "services/backend.h"
#include "services/prefs_store.h"
#include "services/wifi_manager.h"
#include "ui/navigation.h"
#include "ui/more_info_screen.h"
#include "ui/label_preset_selection.h"
#include "ui/ui_common.h"

namespace {
constexpr size_t kPresetCapacity = 64;
static lv_obj_t* s_screen = nullptr;
static lv_obj_t* s_list = nullptr;
static lv_obj_t* s_status = nullptr;
static bool s_open_pending = false;
static bool s_fetch_pending = false;
static bool s_back_pending = false;
static bool s_scan_pending = false;
static bool s_restore_presets_pending = false;
static bool s_m220_pending = false;
static int s_m220_spool = 0, s_m220_preset = 0;
static uint16_t s_m220_width = 576;
static char s_m220_address[18] = {};
static uint16_t s_requested_width = 576;
static M220Device s_found_devices[8];
static lv_obj_t* s_width_label = nullptr;
static FilaManPrintPending s_print;
static int s_spool_id = 0;
static FilaManLabelPreset s_presets[kPresetCapacity];
static size_t s_count = 0;

static void fillList();

static void updateWidth() {
  if (!s_width_label) return;
  char text[24];
  snprintf(text, sizeof(text), "%u px", unsigned(s_m220_width));
  lv_label_set_text(s_width_label, text);
}

static void setStatus(const char* text) {
  if (s_status) lv_label_set_text(s_status, text);
}

static void addPresetRow(int id, const char* name) {
  lv_obj_t* row = lv_btn_create(s_list);
  lv_obj_set_width(row, 392);
  lv_obj_set_height(row, 42);
  lv_obj_set_style_bg_color(row, lv_color_hex(id == prefsGetInt("label_preset", 0) ? 0x174f46 : 0x102035), 0);
  lv_obj_set_style_radius(row, 6, 0);
  lv_obj_add_event_cb(row, labelPresetRowCb, LV_EVENT_CLICKED, (void*)(intptr_t)id);
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
    switch (code) {
      case 401: setStatus(T(STR_LABEL_PC_KEY)); break;
      case 403: setStatus(T(STR_LABEL_PC_SCOPE)); break;
      default: setStatus(T(STR_LABEL_LOAD_FAIL)); break;
    }
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
  lv_obj_set_size(s_list, 420, 104);
  lv_obj_set_pos(s_list, 30, 104);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s_list, 8, 0);
  lv_obj_set_style_pad_row(s_list, 6, 0);
  lv_obj_set_style_bg_color(s_list, lv_color_hex(0x09111e), 0);
  fillList();
  int saved_width = prefsGetInt("m220_width", 576);
  s_m220_width = saved_width >= 384 && saved_width <= 1024 && saved_width % 8 == 0 ? saved_width : 576;

  lv_obj_t* minus = lv_btn_create(s_screen);
  lv_obj_set_size(minus, 42, 34); lv_obj_set_pos(minus, 30, 213);
  lv_obj_add_event_cb(minus, [](lv_event_t*) { if (s_m220_width > 384) { s_m220_width -= 8; prefsPutInt("m220_width", s_m220_width); updateWidth(); } }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* minus_text = lv_label_create(minus); lv_label_set_text(minus_text, "-"); lv_obj_center(minus_text);
  s_width_label = lv_label_create(s_screen); lv_obj_set_pos(s_width_label, 82, 221); updateWidth();
  lv_obj_t* plus = lv_btn_create(s_screen);
  lv_obj_set_size(plus, 42, 34); lv_obj_set_pos(plus, 155, 213);
  lv_obj_add_event_cb(plus, [](lv_event_t*) { if (s_m220_width < 1024) { s_m220_width += 8; prefsPutInt("m220_width", s_m220_width); updateWidth(); } }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* plus_text = lv_label_create(plus); lv_label_set_text(plus_text, "+"); lv_obj_center(plus_text);
  lv_obj_t* scan = lv_btn_create(s_screen);
  lv_obj_set_size(scan, 140, 34); lv_obj_set_pos(scan, 315, 213);
  lv_obj_add_event_cb(scan, [](lv_event_t*) { s_scan_pending = true; }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* scan_text = lv_label_create(scan); lv_label_set_text(scan_text, T(STR_LABEL_M220_SCAN)); lv_obj_center(scan_text);
  lv_obj_t* pc = lv_btn_create(s_screen);
  lv_obj_set_size(pc, 180, 38);
  lv_obj_set_pos(pc, 45, 252);
  lv_obj_add_event_cb(pc, [](lv_event_t*) {
    if (!s_print.request(s_spool_id, prefsGetInt("label_preset", 0))) return;
    setStatus(T(STR_LABEL_PC_PENDING));
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* pc_label = lv_label_create(pc);
  lv_label_set_text(pc_label, T(STR_LABEL_PC_OPEN));
  lv_obj_center(pc_label);
  lv_obj_t* printer = lv_btn_create(s_screen);
  lv_obj_set_size(printer, 180, 38); lv_obj_set_pos(printer, 255, 252);
  lv_obj_add_event_cb(printer, [](lv_event_t*) {
    if (s_m220_pending || s_m220_spool) return;
    String address = prefsGetString("m220_addr");
    if (address.isEmpty()) { setStatus(T(STR_LABEL_M220_SELECT)); return; }
    snprintf(s_m220_address, sizeof(s_m220_address), "%s", address.c_str());
    s_m220_spool = s_spool_id;
    s_m220_preset = prefsGetInt("label_preset", 0);
    s_requested_width = s_m220_width;
    s_m220_pending = true;
    setStatus(T(STR_LABEL_M220_FETCH));
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* printer_text = lv_label_create(printer);
  lv_label_set_text(printer_text, T(STR_LABEL_M220_PRINT)); lv_obj_center(printer_text);
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
    s_print.cancel();
    s_fetch_pending = false;
    s_scan_pending = s_m220_pending = s_restore_presets_pending = false;
    s_m220_spool = 0;
    s_open_pending = false;
    releaseScreen(&s_screen);
    s_list = nullptr;
    s_status = nullptr;
    s_width_label = nullptr;
    showMoreInfoScreen();
    return;
  }
  if (s_open_pending) { s_open_pending = false; showScreen(); }
  if (s_fetch_pending) { s_fetch_pending = false; fetchPresets(); }
  if (s_restore_presets_pending) { s_restore_presets_pending = false; fillList(); }
  if (s_scan_pending && s_screen) {
    s_scan_pending = false;
    setStatus(T(STR_LABEL_M220_SCAN));
    size_t count = phomemoM220Scan(s_found_devices, 8);
    if (s_list) {
      lv_obj_clean(s_list);
      for (size_t i = 0; i < count; ++i) {
        lv_obj_t* row = lv_btn_create(s_list);
        lv_obj_set_size(row, 392, 42);
        lv_obj_t* label = lv_label_create(row);
        lv_label_set_text_fmt(label, "%s %s", s_found_devices[i].name, s_found_devices[i].address);
        lv_obj_center(label);
        lv_obj_add_event_cb(row, [](lv_event_t* event) {
          const char* address = static_cast<const char*>(lv_event_get_user_data(event));
          prefsPutString("m220_addr", address);
          s_restore_presets_pending = true;
          setStatus(address);
        }, LV_EVENT_CLICKED, s_found_devices[i].address);
      }
    }
    if (!count) { fillList(); setStatus(T(STR_LABEL_M220_NONE)); }
  }
  if (s_m220_pending && s_screen) {
    s_m220_pending = false;
    LabelRaster image{};
    int code = -1;
    if (wifiManagerIsConnected()) {
      HttpStallTime stall;
      code = filamanFetchMonoLabel(backendBaseUrl(), filamanApiKey(),
                                   s_m220_spool, s_m220_preset, s_requested_width, &image);
    }
    s_m220_spool = 0;
    if (code == 200) {
      setStatus(T(STR_LABEL_M220_SEND));
      char error[80];
      bool sent = phomemoM220Print(s_m220_address, image, error, sizeof(error));
      setStatus(sent ? T(STR_LABEL_M220_SENT) : error);
    } else {
      switch (code) {
        case 401: setStatus(T(STR_LABEL_PC_KEY)); break;
        case 403: setStatus(T(STR_LABEL_PC_SCOPE)); break;
        case 404: fetchPresets(); setStatus(T(STR_LABEL_PC_MISSING)); break;
        case 422: setStatus(T(STR_LABEL_PC_INVALID)); break;
        default: setStatus(T(wifiManagerIsConnected() ? STR_LABEL_M220_FAILED : STR_LABEL_NO_WIFI)); break;
      }
    }
    filamanFreeLabel(&image);
  }
  int print_spool_id = 0, print_preset_id = 0;
  if (s_screen && s_print.take(&print_spool_id, &print_preset_id)) {
    int request_id = 0;
    int code = -1;
    if (wifiManagerIsConnected()) {
      HttpStallTime stall;
      code = filamanRequestLabelPrint(backendBaseUrl(), filamanApiKey(),
                                      print_spool_id, print_preset_id, &request_id);
    }
    switch (code) {
      case 201: setStatus(T(STR_LABEL_PC_QUEUED)); break;
      case 401: setStatus(T(STR_LABEL_PC_KEY)); break;
      case 403: setStatus(T(STR_LABEL_PC_SCOPE)); break;
      case 404: fetchPresets(); setStatus(T(STR_LABEL_PC_MISSING)); break;
      case 422: setStatus(T(STR_LABEL_PC_INVALID)); break;
      default: setStatus(T(wifiManagerIsConnected() ? STR_LABEL_PC_FAILED : STR_LABEL_NO_WIFI)); break;
    }
  }
}

void hideLabelPrintOverlays() {
  s_print.cancel();
  releaseScreen(&s_screen);
  s_list = nullptr;
  s_status = nullptr;
  s_fetch_pending = false;
  s_open_pending = false;
  s_back_pending = false;
  s_scan_pending = s_m220_pending = s_restore_presets_pending = false;
  s_m220_spool = 0;
  s_width_label = nullptr;
}
