#include "label_print_screen.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "app/app_state.h"
#include "services/backend.h"
#include "services/filaman_api.h"
#include "services/filaman_labels.h"
#include "services/filaman_print_pending.h"
#include "services/label_printer.h"
#include "services/http_progress.h"
#include "services/prefs_store.h"
#include "services/wifi_manager.h"
#include "ui/label_preset_selection.h"
#include "ui/label_preset_group.h"
#include "ui/loading_overlay.h"
#include "ui/more_info_screen.h"
#include "ui/navigation.h"
#include "ui/printer_settings_screen.h"
#include "ui/ui_common.h"
#include "lang.h"

namespace {
constexpr size_t kPresetCapacity = 100;
constexpr size_t kPresetsPerPage = 8;
constexpr const char* kPresetGroups[] = {"A-F", "G-L", "M-R", "S-Z", "#"};
lv_obj_t* screen = nullptr;
lv_obj_t* list = nullptr;
lv_obj_t* status = nullptr;
lv_obj_t* pc_button = nullptr;
lv_obj_t* printer_button = nullptr;
FilaManLabelPreset presets[kPresetCapacity];
size_t preset_count = 0;
LabelRaster preview{};
FilaManPrintPending pc_request;
int spool_id = 0;
bool preset_page = false;
bool preset_from_preview = false;
int preset_group = -1;
size_t preset_page_index = 0;
bool open_pending = false;
bool settings_pending = false;
bool change_pending = false;
bool back_pending = false;
bool fetch_pending = false;
bool refresh_rows_pending = false;
bool print_pending = false;
LabelPrinterConfig preview_printer{};

void setStatus(const char* message) {
  if (status) lv_label_set_text(status, message);
}

void setHttpError(const char* message, int code) {
  if (status) lv_label_set_text_fmt(status, "%s (%d)", message, code);
}

void setMediaMismatch(const LabelPrinterConfig& printer) {
  char message[96];
  snprintf(message, sizeof(message), T(STR_LABEL_PRINTER_MEDIA),
           unsigned(printer.media_width_mm), unsigned(printer.media_length_mm));
  setStatus(message);
}

void addPresetRow(int id, const char* name) {
  if (!lvPoolHasRoomForRow()) return;
  const bool selected = id == prefsGetInt("label_preset", 0);
  lv_obj_t* row = lv_btn_create(list);
  lv_obj_set_size(row, 392, 42);
  styleListRow(row, selected);
  lv_obj_add_event_cb(row, labelPresetRowCb, LV_EVENT_CLICKED, (void*)(intptr_t)id);
  lv_obj_t* label = lv_label_create(row);
  if (id) lv_label_set_text_fmt(label, "#%d  %s", id, name);
  else lv_label_set_text(label, name);
  lv_obj_set_style_text_color(label, lv_color_hex(selected ? UI_COL_ACCENT : UI_COL_INK_2), 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, 360);
  lv_obj_center(label);
}

void fillPresetList() {
  if (!list) return;
  lv_obj_clean(list);
  if (!labelPresetsUseGroups(preset_count)) {
    preset_group = -1;
    addPresetRow(0, T(STR_LABEL_DEFAULT));
    for (size_t i = 0; i < preset_count; ++i) addPresetRow(presets[i].id, presets[i].name);
    return;
  }
  if (preset_group < 0) {
    addPresetRow(0, T(STR_LABEL_DEFAULT));
    for (int group = 0; group < 5; ++group) {
      size_t count = 0;
      for (size_t i = 0; i < preset_count; ++i)
        if (labelPresetGroup(presets[i].name) == group) ++count;
      if (!count || !lvPoolHasRoomForRow()) continue;
      lv_obj_t* row = lv_btn_create(list);
      lv_obj_set_size(row, 392, 42);
      styleListRow(row);
      lv_obj_add_event_cb(row, [](lv_event_t* e) {
        preset_group = (int)(intptr_t)lv_event_get_user_data(e);
        preset_page_index = 0;
        requestLabelPresetRefresh();
      }, LV_EVENT_CLICKED, (void*)(intptr_t)group);
      lv_obj_t* label = lv_label_create(row);
      lv_label_set_text_fmt(label, "%s  (%u)", kPresetGroups[group], (unsigned)count);
      lv_obj_set_style_text_color(label, lv_color_hex(UI_COL_INK_2), 0);
      lv_obj_center(label);
    }
  } else {
    size_t matching = 0;
    for (size_t i = 0; i < preset_count; ++i)
      if (labelPresetGroup(presets[i].name) == preset_group) ++matching;
    const size_t pages = (matching + kPresetsPerPage - 1) / kPresetsPerPage;
    if (pages && preset_page_index >= pages) preset_page_index = pages - 1;
    auto addPageButton = [pages](int direction) {
      if (!lvPoolHasRoomForRow()) return;
      lv_obj_t* row = lv_btn_create(list);
      lv_obj_set_size(row, 392, 42);
      styleOutlineButton(row);
      lv_obj_add_event_cb(row, [](lv_event_t* e) {
        if ((intptr_t)lv_event_get_user_data(e) < 0) --preset_page_index;
        else ++preset_page_index;
        requestLabelPresetRefresh();
      }, LV_EVENT_CLICKED, (void*)(intptr_t)direction);
      lv_obj_t* label = lv_label_create(row);
      lv_label_set_text_fmt(label, "%s %u/%u", direction < 0 ? "<" : ">",
                            (unsigned)(preset_page_index + 1), (unsigned)pages);
      lv_obj_set_style_text_color(label, lv_color_hex(UI_COL_INK_2), 0);
      lv_obj_center(label);
    };
    if (preset_page_index) addPageButton(-1);
    if (preset_page_index + 1 < pages) addPageButton(1);
    size_t position = 0;
    for (size_t i = 0; i < preset_count; ++i) {
      if (labelPresetGroup(presets[i].name) != preset_group) continue;
      if (position >= preset_page_index * kPresetsPerPage &&
          position < (preset_page_index + 1) * kPresetsPerPage)
        addPresetRow(presets[i].id, presets[i].name);
      ++position;
    }
  }
}

void fetchPresets() {
  if (!screen || !preset_page) return;
  if (!wifiManagerIsConnected()) { setStatus(T(STR_LABEL_NO_WIFI)); return; }
  loadingOverlayShow(T(STR_LABEL_LOADING));
  int code;
  {
    HttpStall stall(loadingOverlayProgress);
    code = filamanListLabelPresets(backendBaseUrl(), filamanApiKey(),
                                  presets, kPresetCapacity, &preset_count);
  }
  loadingOverlayHide();
  if (code != 200) {
    preset_count = 0;
    fillPresetList();
    switch (code) {
      case 401: setStatus(T(STR_LABEL_PC_KEY)); break;
      case 403: setStatus(T(STR_LABEL_PC_SCOPE)); break;
      default: setHttpError(T(STR_LABEL_LOAD_FAIL), code); break;
    }
    return;
  }
  const int selected = prefsGetInt("label_preset", 0);
  bool found = selected == 0;
  for (size_t i = 0; i < preset_count; ++i)
    if (presets[i].id == selected) found = true;
  if (!found) {
    prefsPutInt("label_preset", 0);
    setStatus(T(STR_LABEL_PRESET_REMOVED));
  } else if (!preset_count) setStatus(T(STR_LABEL_NONE));
  else setStatus("");
  fillPresetList();
}

void buildPresetScreen() {
  releaseScreen(&screen);
  filamanFreeLabel(&preview);
  list = status = pc_button = printer_button = nullptr;
  preset_page = true;
  preset_group = -1;
  preset_page_index = 0;
  screen = buildOverlayScreen();
  buildSubHeader(screen, T(STR_LABEL_PRESET_TITLE), [](lv_event_t*) { back_pending = true; });

  lv_obj_t* refresh = lv_btn_create(screen);
  lv_obj_set_size(refresh, 100, 34);
  lv_obj_set_pos(refresh, 320, 8);
  styleOutlineButton(refresh);
  lv_obj_add_event_cb(refresh, [](lv_event_t*) { fetch_pending = true; }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* refresh_label = lv_label_create(refresh);
  lv_label_set_text(refresh_label, T(STR_LABEL_REFRESH));
  lv_obj_set_style_text_color(refresh_label, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_center(refresh_label);

  status = lv_label_create(screen);
  lv_label_set_text(status, "");
  lv_obj_set_width(status, 440);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(status, lv_color_hex(0xb7c9dc), 0);
  lv_obj_set_pos(status, 20, 56);

  list = lv_obj_create(screen);
  lv_obj_set_size(list, 420, 215);
  lv_obj_set_pos(list, 30, 91);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(list, 8, 0);
  lv_obj_set_style_pad_row(list, 6, 0);
  styleListPanel(list);
  fillPresetList();
  fetch_pending = true;
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

// The server's packed mono1 raster can be displayed without a PNG decoder.
// Keep a small indexed thumbnail on screen and the full raster for BLE.
bool drawPreview() {
  const uint32_t width = preview.rotated ? preview.height : preview.content_width;
  const uint32_t height = preview.rotated ? preview.content_width : preview.height;
  const bool width_limited = width * 160 > height * 420;
  const uint32_t target_width = width_limited ? 420 : width * 160 / height;
  const uint32_t target_height = width_limited ? height * 420 / width : 160;
  if (!target_width || !target_height) return false;
  const size_t row_bytes = (target_width + 7) / 8;
  const size_t bytes = 8 + row_bytes * target_height;
  uint8_t* thumbnail = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
  if (!thumbnail) return false;
  memset(thumbnail, 0, bytes);
  for (uint32_t y = 0; y < target_height; ++y) {
    const uint32_t source_y = y * height / target_height;
    for (uint32_t x = 0; x < target_width; ++x) {
      const uint32_t source_x = x * width / target_width;
      if (labelRasterPreviewBlack(preview, source_x, source_y))
        thumbnail[8 + y * row_bytes + x / 8] |= 0x80 >> (x % 8);
    }
  }
  lv_obj_t* canvas = lv_canvas_create(screen);
  lv_canvas_set_buffer(canvas, thumbnail, target_width, target_height, LV_IMG_CF_INDEXED_1BIT);
  lv_canvas_set_palette(canvas, 0, lv_color_white());
  lv_canvas_set_palette(canvas, 1, lv_color_black());
  lv_obj_add_event_cb(canvas, [](lv_event_t* e) {
    free(lv_event_get_user_data(e));
  }, LV_EVENT_DELETE, thumbnail);
  lv_obj_set_pos(canvas, (480 - target_width) / 2, 92 + (160 - target_height) / 2);
  return true;
}

void fetchPreview() {
  if (!screen || preset_page) return;
  if (!wifiManagerIsConnected()) { setStatus(T(STR_LABEL_NO_WIFI)); return; }
  preview_printer = labelPrinterLoadConfig();
  const uint16_t width = labelPrinterRasterWidth(preview_printer.model,
                                                preview_printer.media_width_mm);
  const char* orientation = preview_printer.media_width_mm >= preview_printer.media_length_mm
      ? "landscape" : "portrait";
  loadingOverlayShow(T(STR_LABEL_PRINTER_FETCH));
  int code;
  bool drawn = false;
  {
    HttpStall stall(loadingOverlayProgress);
    code = filamanFetchMonoLabel(backendBaseUrl(), filamanApiKey(),
                                spool_id, prefsGetInt("label_preset", 0),
                                width, orientation, &preview);
    if (code == 200) drawn = drawPreview();
  }
  loadingOverlayHide();
  if (code != 200) {
    switch (code) {
      case 401: setStatus(T(STR_LABEL_PC_KEY)); break;
      case 403: setStatus(T(STR_LABEL_PC_SCOPE)); break;
      case FILAMAN_LABEL_NO_PSRAM: setStatus(T(STR_LABEL_PRINTER_NO_PSRAM)); break;
      default: setHttpError(T(STR_LABEL_PRINTER_FAILED), code); break;
    }
    return;
  }
  if (!drawn) {
    setStatus(T(STR_LABEL_PRINTER_NO_PSRAM));
    filamanFreeLabel(&preview);
    return;
  }
  lv_obj_clear_state(pc_button, LV_STATE_DISABLED);
  if (labelPrinterRasterFits(preview_printer.model, preview, preview_printer.media_width_mm,
                            preview_printer.media_length_mm)) {
    setStatus("");
    lv_obj_clear_state(printer_button, LV_STATE_DISABLED);
  } else {
    setMediaMismatch(preview_printer);
    lv_obj_add_state(printer_button, LV_STATE_DISABLED);
  }
}

void buildPreviewScreen() {
  releaseScreen(&screen);
  filamanFreeLabel(&preview);
  list = status = pc_button = printer_button = nullptr;
  preset_page = false;
  screen = buildOverlayScreen();
  buildSubHeader(screen, T(STR_LABEL_PREVIEW), [](lv_event_t*) { back_pending = true; });

  lv_obj_t* change = lv_btn_create(screen);
  lv_obj_set_size(change, 140, 44);
  lv_obj_set_pos(change, 325, 268);
  styleOutlineButton(change);
  lv_obj_set_style_pad_all(change, 4, 0);
  lv_obj_add_event_cb(change, [](lv_event_t*) { change_pending = true; }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* change_label = lv_label_create(change);
  lv_label_set_text(change_label, T(STR_LABEL_CHANGE_PRESET));
  lv_obj_set_style_text_color(change_label, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(change_label, UI_FONT_SMALL, 0);
  lv_obj_center(change_label);

  status = lv_label_create(screen);
  lv_label_set_text(status, "");
  lv_obj_set_width(status, 440);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(status, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_pos(status, 20, 50);
  lv_obj_set_height(status, 40);
  lv_obj_set_style_text_font(status, UI_FONT_BODY, 0);
  lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);

  pc_button = lv_btn_create(screen);
  lv_obj_set_size(pc_button, 140, 44);
  lv_obj_set_pos(pc_button, 15, 268);
  styleOutlineButton(pc_button);
  lv_obj_set_style_pad_all(pc_button, 4, 0);
  lv_obj_add_event_cb(pc_button, [](lv_event_t*) {
    if (preview.pixels && pc_request.request(spool_id, prefsGetInt("label_preset", 0)))
      setStatus(T(STR_LABEL_PC_PENDING));
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* pc_label = lv_label_create(pc_button);
  lv_label_set_text(pc_label, T(STR_LABEL_PC_OPEN));
  lv_obj_set_style_text_color(pc_label, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(pc_label, UI_FONT_SMALL, 0);
  lv_obj_center(pc_label);
  lv_obj_add_state(pc_button, LV_STATE_DISABLED);

  printer_button = lv_btn_create(screen);
  lv_obj_set_size(printer_button, 140, 44);
  lv_obj_set_pos(printer_button, 170, 268);
  styleOutlineButton(printer_button);
  lv_obj_set_style_pad_all(printer_button, 4, 0);
  lv_obj_add_event_cb(printer_button, [](lv_event_t*) {
    if (preview.pixels) print_pending = true;
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* printer_label = lv_label_create(printer_button);
  lv_label_set_text(printer_label, T(STR_LABEL_PRINTER_PRINT));
  lv_obj_set_style_text_color(printer_label, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(printer_label, UI_FONT_SMALL, 0);
  lv_obj_center(printer_label);
  lv_obj_add_state(printer_button, LV_STATE_DISABLED);

  fetch_pending = true;
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
}
}  // namespace

void requestLabelPreviewScreen(int selected_spool_id) {
  spool_id = selected_spool_id;
  open_pending = true;
}

void requestLabelPresetSettingsScreen() { settings_pending = true; }

void requestLabelPresetRefresh() { refresh_rows_pending = true; }

void handleLabelPrintDeferredActions() {
  if (back_pending) {
    back_pending = false;
    if (preset_page) {
      if (preset_group >= 0) {
        preset_group = -1;
        preset_page_index = 0;
        fillPresetList();
      } else if (preset_from_preview) buildPreviewScreen();
      else {
        hideLabelPrintOverlays();
        requestPrinterSettingsScreen();
      }
    } else {
      hideLabelPrintOverlays();
      showMoreInfoScreen();
    }
    return;
  }
  if (open_pending) {
    open_pending = false;
    hideAllOverlays();
    buildPreviewScreen();
  }
  if (settings_pending) {
    settings_pending = false;
    preset_from_preview = false;
    hideAllOverlays();
    buildPresetScreen();
  }
  if (change_pending) {
    change_pending = false;
    preset_from_preview = true;
    buildPresetScreen();
  }
  if (refresh_rows_pending) {
    refresh_rows_pending = false;
    fillPresetList();
  }
  if (fetch_pending) {
    fetch_pending = false;
    if (preset_page) fetchPresets();
    else fetchPreview();
  }
  if (print_pending && screen && !preset_page) {
    print_pending = false;
    const LabelPrinterConfig current = labelPrinterLoadConfig();
    if (!labelPrinterConfigured(current)) setStatus(T(STR_LABEL_PRINTER_SELECT));
    else if (preview.pixels) {
      if (!labelPrinterRasterFits(current.model, preview, current.media_width_mm,
                                 current.media_length_mm)) {
        setMediaMismatch(current);
        return;
      }
      char send_message[64];
      snprintf(send_message, sizeof(send_message), T(STR_LABEL_PRINTER_SEND),
               labelPrinterProfile(current.model).name);
      loadingOverlayShow(send_message);
      char error[80];
      // The label is already in PSRAM. Free the WiFi stack's internal heap
      // while the BLE stack sends it, then reconnect for the next API call.
      const bool resume_wifi = wifiManagerIsConnected();
      if (resume_wifi) {
        WiFi.disconnect(true);
        delay(100);
        Serial.printf("Label printer WiFi stopped: heap=%u\n", unsigned(ESP.getFreeHeap()));
      }
      const bool sent = labelPrinterPrint(current, preview, error, sizeof(error),
                                         loadingOverlayTick);
      if (resume_wifi) wifiManagerBegin(cfg_wifi_ssid, cfg_wifi_password);
      loadingOverlayHide();
      setStatus(sent ? T(STR_LABEL_PRINTER_SENT) : error);
    }
  }
  int request_spool = 0, request_preset = 0;
  if (screen && pc_request.take(&request_spool, &request_preset)) {
    int request_id = 0;
    int code = -1;
    if (wifiManagerIsConnected()) {
      loadingOverlayShow(T(STR_LABEL_PC_PENDING));
      {
        HttpStall stall(loadingOverlayProgress);
        code = filamanRequestLabelPrint(backendBaseUrl(), filamanApiKey(),
                                       request_spool, request_preset, &request_id);
      }
      loadingOverlayHide();
    }
    switch (code) {
      case 201: setStatus(T(STR_LABEL_PC_QUEUED)); break;
      case 401: setStatus(T(STR_LABEL_PC_KEY)); break;
      case 403: setStatus(T(STR_LABEL_PC_SCOPE)); break;
      case 404: setStatus(T(STR_LABEL_PC_MISSING)); break;
      case 422: setStatus(T(STR_LABEL_PC_INVALID)); break;
      default: setStatus(T(wifiManagerIsConnected() ? STR_LABEL_PC_FAILED : STR_LABEL_NO_WIFI)); break;
    }
  }
}

void hideLabelPrintOverlays() {
  pc_request.cancel();
  releaseScreen(&screen);
  filamanFreeLabel(&preview);
  list = status = pc_button = printer_button = nullptr;
  open_pending = settings_pending = change_pending = back_pending = false;
  fetch_pending = refresh_rows_pending = print_pending = false;
  preset_page = preset_from_preview = false;
  preset_group = -1;
  preset_page_index = 0;
}
