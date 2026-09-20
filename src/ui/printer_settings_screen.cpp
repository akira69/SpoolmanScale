#include "printer_settings_screen.h"

#include <Arduino.h>
#include <lvgl.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/backend.h"
#include "services/phomemo_m220.h"
#include "services/prefs_store.h"
#include "ui/connection_screen.h"
#include "ui/header_status.h"
#include "ui/label_print_screen.h"
#include "ui/navigation.h"
#include "ui/ui_common.h"

namespace {
lv_obj_t* screen = nullptr;
lv_obj_t* list = nullptr;
lv_obj_t* status = nullptr;
lv_obj_t* width_label = nullptr;
M220Device devices[8];
bool open_pending = false;
bool back_pending = false;
bool scan_pending = false;
uint16_t width = 576;

void setStatus(const char* text) {
  if (status) lv_label_set_text(status, text);
}

void updateWidth() {
  if (width_label) lv_label_set_text_fmt(width_label, "%u px", unsigned(width));
}

void updateSavedAddress() {
  String address = prefsGetString("m220_addr");
  if (address.isEmpty()) setStatus(T(STR_PRINTER_NONE));
  else {
    char text[56];
    snprintf(text, sizeof(text), T(STR_PRINTER_SELECTED_FMT), address.c_str());
    setStatus(text);
  }
  updateHeaderStatus();
}

void buildScreen() {
  if (!backendIsFilaMan()) return;
  hideAllOverlays();
  closeConnectionScreen();
  screen = buildOverlayScreen();
  buildSubHeader(screen, T(STR_PRINTER_TITLE), [](lv_event_t*) { back_pending = true; });

  lv_obj_t* preset = lv_btn_create(screen);
  lv_obj_set_size(preset, 112, 34);
  lv_obj_set_pos(preset, 352, 8);
  styleOutlineButton(preset);
  lv_obj_add_event_cb(preset, [](lv_event_t*) { requestLabelPresetSettingsScreen(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* preset_text = lv_label_create(preset);
  lv_label_set_text(preset_text, T(STR_LABEL_PRESET_TITLE));
  lv_obj_set_style_text_color(preset_text, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_center(preset_text);

  lv_obj_t* scan = lv_btn_create(screen);
  lv_obj_set_size(scan, 210, 38);
  lv_obj_set_pos(scan, 20, 56);
  styleOutlineButton(scan);
  lv_obj_add_event_cb(scan, [](lv_event_t*) { scan_pending = true; }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* scan_text = lv_label_create(scan);
  lv_label_set_text_fmt(scan_text, "%s  %s", LV_SYMBOL_GPS, T(STR_LABEL_M220_SCAN));
  lv_obj_set_style_text_color(scan_text, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_center(scan_text);

  lv_obj_t* clear = lv_btn_create(screen);
  lv_obj_set_size(clear, 210, 38);
  lv_obj_set_pos(clear, 250, 56);
  styleOutlineButton(clear);
  lv_obj_add_event_cb(clear, [](lv_event_t*) {
    prefsPutString("m220_addr", "");
    updateSavedAddress();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* clear_text = lv_label_create(clear);
  lv_label_set_text_fmt(clear_text, "%s  %s", LV_SYMBOL_TRASH, T(STR_PRINTER_CLEAR));
  lv_obj_set_style_text_color(clear_text, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_center(clear_text);

  status = lv_label_create(screen);
  lv_obj_set_width(status, 440);
  lv_obj_set_pos(status, 20, 108);
  lv_obj_set_style_text_color(status, lv_color_hex(0xb7c9dc), 0);

  list = lv_obj_create(screen);
  lv_obj_set_size(list, 440, 110);
  lv_obj_set_pos(list, 20, 140);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(list, 4, 0);
  lv_obj_set_style_pad_row(list, 4, 0);
  styleListPanel(list);

  int saved = prefsGetInt("m220_width", 576);
  width = saved >= 384 && saved <= 576 && saved % 8 == 0 ? saved : 576;
  lv_obj_t* minus = lv_btn_create(screen);
  lv_obj_set_size(minus, 60, 38);
  lv_obj_set_pos(minus, 110, 266);
  styleOutlineButton(minus);
  lv_obj_add_event_cb(minus, [](lv_event_t*) {
    if (width > 384) { width -= 8; prefsPutInt("m220_width", width); updateWidth(); }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* minus_text = lv_label_create(minus);
  lv_label_set_text(minus_text, "-");
  lv_obj_set_style_text_color(minus_text, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_center(minus_text);

  width_label = lv_label_create(screen);
  lv_obj_set_width(width_label, 120);
  lv_obj_set_style_text_align(width_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(width_label, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_pos(width_label, 180, 276);
  updateWidth();

  lv_obj_t* plus = lv_btn_create(screen);
  lv_obj_set_size(plus, 60, 38);
  lv_obj_set_pos(plus, 310, 266);
  styleOutlineButton(plus);
  lv_obj_add_event_cb(plus, [](lv_event_t*) {
    if (width < 576) { width += 8; prefsPutInt("m220_width", width); updateWidth(); }
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* plus_text = lv_label_create(plus);
  lv_label_set_text(plus_text, "+");
  lv_obj_set_style_text_color(plus_text, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_center(plus_text);
  updateSavedAddress();
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
}
}  // namespace

void requestPrinterSettingsScreen() { open_pending = true; }

void handlePrinterSettingsDeferredActions() {
  if (back_pending) {
    back_pending = false;
    hidePrinterSettingsOverlays();
    closeConnectionScreen();
    buildConnectionScreen();
    hideAllOverlays();
    lv_obj_clear_flag(scr_connection, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (open_pending) { open_pending = false; buildScreen(); }
  if (scan_pending && screen) {
    scan_pending = false;
    setStatus(T(STR_PRINTER_SCANNING));
    lv_refr_now(nullptr);
    size_t count = phomemoM220Scan(devices, 8);
    lv_obj_clean(list);
    for (size_t i = 0; i < count && lvPoolHasRoomForRow(); ++i) {
      lv_obj_t* row = lv_btn_create(list);
      lv_obj_set_size(row, 420, 42);
      styleListRow(row);
      lv_obj_add_event_cb(row, [](lv_event_t* e) {
        const char* address = static_cast<const char*>(lv_event_get_user_data(e));
        prefsPutString("m220_addr", address);
        updateSavedAddress();
      }, LV_EVENT_CLICKED, devices[i].address);
      lv_obj_t* label = lv_label_create(row);
      lv_label_set_text_fmt(label, "%s  %s", devices[i].name, devices[i].address);
      lv_obj_set_style_text_color(label, lv_color_hex(UI_COL_INK_2), 0);
      lv_obj_set_width(label, 400);
      lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
      lv_obj_center(label);
    }
    if (!count) setStatus(T(STR_LABEL_M220_NONE));
    else updateSavedAddress();
  }
}

void hidePrinterSettingsOverlays() {
  releaseScreen(&screen);
  list = status = width_label = nullptr;
  open_pending = back_pending = scan_pending = false;
}
