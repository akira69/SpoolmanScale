#include "printer_settings_screen.h"

#include <Arduino.h>
#include <cstring>
#include <lvgl.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/backend.h"
#include "services/label_printer.h"
#include "ui/connection_screen.h"
#include "ui/header_status.h"
#include "ui/label_print_screen.h"
#include "ui/loading_overlay.h"
#include "ui/info_popup.h"
#include "ui/navigation.h"
#include "ui/ui_common.h"

namespace {
enum class Page { MAIN, MODELS, SIZES, CUSTOM };
struct MediaSize { uint8_t width, length; };
struct Adjustment { bool width; int8_t amount; };

constexpr MediaSize kMediaSizes[] = {
  {20, 30}, {25, 50}, {30, 20}, {30, 40}, {40, 12}, {40, 30},
  {40, 60}, {50, 25}, {50, 30}, {50, 50}, {50, 80}, {60, 40}, {70, 80},
};
constexpr Adjustment kAdjustments[] = {
  {true, -1}, {true, 1}, {false, -1}, {false, 1},
};

lv_obj_t* screen = nullptr;
lv_obj_t* list = nullptr;
lv_obj_t* selected_value = nullptr;
lv_obj_t* selected_address = nullptr;
lv_obj_t* forget_button = nullptr;
lv_obj_t* device_rows[24]{};
lv_obj_t* device_names[24]{};
lv_obj_t* custom_width_label = nullptr;
lv_obj_t* custom_length_label = nullptr;
lv_obj_t* custom_save_label = nullptr;
LabelPrinterDevice devices[24];
LabelPrinterConfig config{};
size_t device_count = 0;
Page page = Page::MAIN;
bool open_pending = false;
bool back_pending = false;
bool scan_pending = false;
bool sizes_pending = false;
bool models_pending = false;
bool custom_pending = false;
bool main_pending = false;
uint16_t custom_width = 0;
uint16_t custom_length = 0;

bool saveConfig(const LabelPrinterConfig& next) {
  if (!labelPrinterSaveConfig(next)) {
    showInfoPopup(STR_PRINTER_TITLE, STR_ERR_SAVE, INFO_WARN);
    return false;
  }
  config = next;
  return true;
}

bool mediaFits(const LabelPrinterProfile& profile, uint16_t width, uint16_t length) {
  return width >= profile.min_width_mm && width <= profile.max_width_mm &&
         length >= profile.min_length_mm && length <= profile.max_length_mm;
}

bool saveMedia(uint16_t width, uint16_t length) {
  LabelPrinterConfig next = config;
  next.media_width_mm = width;
  next.media_length_mm = length;
  return saveConfig(next);
}

const char* deviceName(const LabelPrinterDevice& device) {
  return device.name[0] ? device.name : T(STR_PRINTER_UNKNOWN_DEVICE);
}

lv_obj_t* addText(lv_obj_t* parent, const char* text, const lv_font_t* font,
                  uint32_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  return label;
}

void clearPointers() {
  list = selected_value = selected_address = forget_button = nullptr;
  custom_width_label = custom_length_label = custom_save_label = nullptr;
  for (size_t i = 0; i < 24; ++i) device_rows[i] = device_names[i] = nullptr;
}

void beginScreen(const char* title, Page next_page) {
  releaseScreen(&screen);
  clearPointers();
  page = next_page;
  screen = buildOverlayScreen();
  buildSubHeader(screen, title, [](lv_event_t*) { back_pending = true; });
}

void refreshDeviceSelection() {
  for (size_t i = 0; i < device_count; ++i) {
    if (!device_rows[i] || !device_names[i]) continue;
    const bool selected = config.address[0] && !strcmp(config.address, devices[i].address);
    styleListRow(device_rows[i], selected);
    if (selected) lv_label_set_text_fmt(device_names[i], "%s  %s", LV_SYMBOL_OK, deviceName(devices[i]));
    else lv_label_set_text(device_names[i], deviceName(devices[i]));
    lv_obj_set_style_text_color(device_names[i],
        lv_color_hex(selected ? UI_COL_ACCENT : UI_COL_INK_2), 0);
  }
}

void updateSavedAddress() {
  if (!selected_value || !selected_address || !forget_button) return;
  if (!config.address[0]) {
    lv_label_set_text(selected_value, T(STR_PRINTER_NONE));
    lv_label_set_text(selected_address, "");
    lv_obj_set_style_text_color(selected_value, lv_color_hex(UI_COL_INK_SOFT), 0);
    lv_obj_add_flag(forget_button, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_label_set_text(selected_value, config.name[0] ? config.name : config.address);
    lv_label_set_text(selected_address, config.name[0] ? config.address : "");
    lv_obj_set_style_text_color(selected_value, lv_color_hex(UI_COL_INK_2), 0);
    lv_obj_clear_flag(forget_button, LV_OBJ_FLAG_HIDDEN);
  }
  refreshDeviceSelection();
  updateHeaderStatus();
}

void showListMessage(const char* text) {
  if (!list) return;
  lv_obj_clean(list);
  device_count = 0;
  lv_obj_t* message = addText(list, text, UI_FONT_SMALL, UI_COL_INK_SOFT);
  lv_obj_set_width(message, 410);
  lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(message);
}

void renderDevices() {
  if (!list) return;
  lv_obj_clean(list);
  for (size_t i = 0; i < 24; ++i) device_rows[i] = device_names[i] = nullptr;
  if (!device_count) {
    showListMessage(T(STR_PRINTER_SCAN_EMPTY));
    return;
  }
  for (size_t i = 0; i < device_count && lvPoolHasRoomForRow(); ++i) {
    lv_obj_t* row = lv_btn_create(list);
    device_rows[i] = row;
    lv_obj_set_size(row, 424, 50);
    styleListRow(row);
    lv_obj_add_event_cb(row, [](lv_event_t* event) {
      const char* address = static_cast<const char*>(lv_event_get_user_data(event));
      for (size_t i = 0; i < device_count; ++i) {
        if (strcmp(address, devices[i].address)) continue;
        LabelPrinterConfig next = config;
        snprintf(next.name, sizeof(next.name), "%s", devices[i].name);
        snprintf(next.address, sizeof(next.address), "%s", address);
        if (saveConfig(next)) updateSavedAddress();
        break;
      }
    }, LV_EVENT_CLICKED, devices[i].address);

    device_names[i] = addText(row, deviceName(devices[i]), UI_FONT_BODY, UI_COL_INK_2);
    lv_obj_set_width(device_names[i], 210);
    lv_label_set_long_mode(device_names[i], LV_LABEL_LONG_DOT);
    lv_obj_align(device_names[i], LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t* address = addText(row, devices[i].address, UI_FONT_CAPTION, UI_COL_INK_SOFT);
    lv_obj_align(address, LV_ALIGN_RIGHT_MID, -12, 0);
  }
  refreshDeviceSelection();
}

void buildMainScreen() {
  config = labelPrinterLoadConfig();
  beginScreen(T(STR_PRINTER_TITLE), Page::MAIN);

  lv_obj_t* preset = lv_btn_create(screen);
  lv_obj_set_size(preset, 112, 34);
  lv_obj_set_pos(preset, 352, 8);
  styleOutlineButton(preset);
  lv_obj_add_event_cb(preset, [](lv_event_t*) { requestLabelPresetSettingsScreen(); },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t* preset_text = addText(preset, T(STR_LABEL_PRESET_TITLE), UI_FONT_SMALL, UI_COL_INK_2);
  lv_obj_center(preset_text);

  lv_obj_t* selected = lv_obj_create(screen);
  lv_obj_set_size(selected, 456, 58);
  lv_obj_set_pos(selected, 12, 52);
  styleListPanel(selected);
  lv_obj_set_style_pad_all(selected, 0, 0);
  lv_obj_clear_flag(selected, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* selected_title = addText(selected, T(STR_PRINTER_SELECTED), UI_FONT_CAPTION,
                                     UI_COL_CAPTION);
  lv_obj_set_pos(selected_title, 14, 7);
  selected_value = addText(selected, "", UI_FONT_BODY, UI_COL_INK_2);
  lv_obj_set_width(selected_value, 204);
  lv_label_set_long_mode(selected_value, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(selected_value, 14, 23);
  selected_address = addText(selected, "", UI_FONT_CAPTION, UI_COL_INK_SOFT);
  lv_obj_set_pos(selected_address, 14, 40);
  lv_obj_set_width(selected_address, 204);
  lv_label_set_long_mode(selected_address, LV_LABEL_LONG_DOT);

  forget_button = lv_btn_create(selected);
  lv_obj_set_size(forget_button, 96, 44);
  lv_obj_set_pos(forget_button, 228, 7);
  styleOutlineButton(forget_button);
  lv_obj_add_event_cb(forget_button, [](lv_event_t*) {
    LabelPrinterConfig next = config;
    next.address[0] = next.name[0] = '\0';
    if (saveConfig(next)) updateSavedAddress();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* forget_text = addText(forget_button, T(STR_PRINTER_CLEAR), UI_FONT_SMALL, UI_COL_INK_2);
  lv_obj_center(forget_text);

  lv_obj_t* model = lv_btn_create(selected);
  lv_obj_set_size(model, 112, 44);
  lv_obj_set_pos(model, 332, 7);
  styleOutlineButton(model);
  lv_obj_set_style_pad_all(model, 2, 0);
  lv_obj_add_event_cb(model, [](lv_event_t*) { models_pending = true; },
                      LV_EVENT_CLICKED, nullptr);
  const auto& profile = labelPrinterProfile(config.model);
  lv_obj_t* model_text = addText(model, profile.name, UI_FONT_SMALL, UI_COL_INK_2);
  if (profile.experimental)
    lv_label_set_text_fmt(model_text, "%s\n%s", profile.name, T(STR_PRINTER_EXPERIMENTAL));
  lv_obj_set_style_text_align(model_text, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(model_text);

  lv_obj_t* devices_panel = lv_obj_create(screen);
  lv_obj_set_size(devices_panel, 456, 118);
  lv_obj_set_pos(devices_panel, 12, 118);
  styleListPanel(devices_panel);
  lv_obj_set_style_pad_all(devices_panel, 0, 0);
  lv_obj_clear_flag(devices_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* available = addText(devices_panel, T(STR_PRINTER_SELECT_DEVICE), UI_FONT_BODY,
                                UI_COL_INK_2);
  lv_obj_set_pos(available, 12, 14);

  lv_obj_t* scan = lv_btn_create(devices_panel);
  lv_obj_set_size(scan, 140, 44);
  lv_obj_set_pos(scan, 304, 4);
  styleOutlineButton(scan);
  lv_obj_set_style_pad_all(scan, 4, 0);
  lv_obj_add_event_cb(scan, [](lv_event_t*) { scan_pending = true; },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t* scan_text = lv_label_create(scan);
  lv_label_set_text(scan_text, T(STR_PRINTER_SCAN));
  lv_obj_set_style_text_color(scan_text, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(scan_text, UI_FONT_SMALL, 0);
  lv_obj_center(scan_text);

  list = lv_obj_create(devices_panel);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, 440, 60);
  lv_obj_set_pos(list, 8, 52);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, 4, 0);

  lv_obj_t* media = lv_btn_create(screen);
  lv_obj_set_size(media, 456, 68);
  lv_obj_set_pos(media, 12, 244);
  styleListRow(media);
  lv_obj_add_event_cb(media, [](lv_event_t*) { sizes_pending = true; },
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t* media_title = addText(media, T(STR_PRINTER_LOADED_SIZE), UI_FONT_BODY,
                                  UI_COL_INK_2);
  lv_obj_set_pos(media_title, 14, 8);
  lv_obj_t* media_help = addText(media, T(STR_PRINTER_SIZE_HELP), UI_FONT_CAPTION,
                                 UI_COL_CAPTION);
  lv_obj_set_pos(media_help, 14, 39);
  char media_text[24];
  snprintf(media_text, sizeof(media_text), "%u x %u mm",
           unsigned(config.media_width_mm), unsigned(config.media_length_mm));
  lv_obj_t* media_value = addText(media, media_text, UI_FONT_BODY, UI_COL_ACCENT);
  lv_obj_align(media_value, LV_ALIGN_RIGHT_MID, -34, -8);
  lv_obj_t* arrow = addText(media, LV_SYMBOL_RIGHT, UI_FONT_BODY, UI_COL_CAPTION);
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -12, -8);

  updateSavedAddress();
  showListMessage(T(STR_PRINTER_SCAN_HINT));
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void buildModelScreen() {
  beginScreen(T(STR_PRINTER_MODEL), Page::MODELS);
  const LabelPrinterModel models[] = {LabelPrinterModel::M220, LabelPrinterModel::M110};
  int y = 62;
  for (const auto model : models) {
    const auto& profile = labelPrinterProfile(model);
    lv_obj_t* row = lv_btn_create(screen);
    lv_obj_set_size(row, 440, 82);
    lv_obj_set_pos(row, 20, y);
    y += 90;
    styleListRow(row, config.model == model);
    lv_obj_add_event_cb(row, [](lv_event_t* event) {
      LabelPrinterConfig next = config;
      next.model = static_cast<LabelPrinterModel>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
      const auto& profile = labelPrinterProfile(next.model);
      if (!mediaFits(profile, next.media_width_mm, next.media_length_mm)) {
        next.media_width_mm = profile.default_width_mm;
        next.media_length_mm = profile.default_length_mm;
      }
      if (saveConfig(next)) main_pending = true;
    }, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(model)));
    lv_obj_t* name = addText(row, profile.name, UI_FONT_BODY, UI_COL_INK_2);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 14, profile.experimental ? -12 : 0);
    if (profile.experimental) {
      lv_obj_t* caption = addText(row, T(STR_PRINTER_EXPERIMENTAL), UI_FONT_SMALL, UI_COL_CAPTION);
      lv_obj_align(caption, LV_ALIGN_LEFT_MID, 14, 16);
    }
  }
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

bool isCommonSize(uint16_t width, uint16_t length) {
  for (const auto& size : kMediaSizes)
    if (size.width == width && size.length == length) return true;
  return false;
}

void addSizeRow(const char* text, bool selected, lv_event_cb_t callback, void* data) {
  if (!lvPoolHasRoomForRow()) return;
  lv_obj_t* row = lv_btn_create(list);
  lv_obj_set_size(row, 412, 52);
  styleListRow(row, selected);
  lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, data);
  lv_obj_t* label = lv_label_create(row);
  if (selected) lv_label_set_text_fmt(label, "%s  %s", LV_SYMBOL_OK, text);
  else lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(selected ? UI_COL_ACCENT : UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(label, UI_FONT_BODY, 0);
  lv_obj_center(label);
}

void buildSizeScreen() {
  beginScreen(T(STR_PRINTER_LOADED_SIZE), Page::SIZES);
  list = lv_obj_create(screen);
  lv_obj_set_size(list, 440, 258);
  lv_obj_set_pos(list, 20, 54);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(list, 8, 0);
  lv_obj_set_style_pad_row(list, 5, 0);
  styleListPanel(list);

  const uint16_t width = config.media_width_mm;
  const uint16_t length = config.media_length_mm;
  for (const auto& size : kMediaSizes) {
    if (!mediaFits(labelPrinterProfile(config.model), size.width, size.length)) continue;
    char text[20];
    snprintf(text, sizeof(text), "%u x %u mm", unsigned(size.width), unsigned(size.length));
    addSizeRow(text, size.width == width && size.length == length,
      [](lv_event_t* event) {
        const MediaSize* size = static_cast<const MediaSize*>(lv_event_get_user_data(event));
        if (saveMedia(size->width, size->length)) main_pending = true;
      }, const_cast<MediaSize*>(&size));
  }
  char custom_text[40];
  if (isCommonSize(width, length)) snprintf(custom_text, sizeof(custom_text), "%s...", T(STR_PRINTER_CUSTOM_SIZE));
  else snprintf(custom_text, sizeof(custom_text), "%s: %u x %u mm", T(STR_PRINTER_CUSTOM_SIZE),
                unsigned(width), unsigned(length));
  addSizeRow(custom_text, !isCommonSize(width, length),
             [](lv_event_t*) { custom_pending = true; }, nullptr);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
}

void updateCustomLabels() {
  if (custom_width_label) lv_label_set_text_fmt(custom_width_label, "%u mm", unsigned(custom_width));
  if (custom_length_label) lv_label_set_text_fmt(custom_length_label, "%u mm", unsigned(custom_length));
  if (custom_save_label) {
    char text[32];
    snprintf(text, sizeof(text), "%s  %u x %u mm", LV_SYMBOL_OK,
             unsigned(custom_width), unsigned(custom_length));
    lv_label_set_text(custom_save_label, text);
  }
}

void adjustCustom(lv_event_t* event) {
  const Adjustment* adjustment = static_cast<const Adjustment*>(lv_event_get_user_data(event));
  uint16_t& value = adjustment->width ? custom_width : custom_length;
  const auto& profile = labelPrinterProfile(config.model);
  const uint16_t minimum = adjustment->width ? profile.min_width_mm : profile.min_length_mm;
  const uint16_t maximum = adjustment->width ? profile.max_width_mm : profile.max_length_mm;
  const int next = int(value) + adjustment->amount;
  value = next < minimum ? minimum : next > maximum ? maximum : next;
  updateCustomLabels();
}

void addAdjustmentRow(int y, StringID title, bool width) {
  lv_obj_t* row = lv_obj_create(screen);
  lv_obj_set_size(row, 440, 82);
  lv_obj_set_pos(row, 20, y);
  styleListPanel(row);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* title_label = addText(row, T(title), UI_FONT_BODY, UI_COL_INK_2);
  lv_obj_set_pos(title_label, 14, 13);
  const auto& profile = labelPrinterProfile(config.model);
  char range[24];
  snprintf(range, sizeof(range), "%u-%u mm",
           unsigned(width ? profile.min_width_mm : profile.min_length_mm),
           unsigned(width ? profile.max_width_mm : profile.max_length_mm));
  lv_obj_t* range_label = addText(row, range, UI_FONT_CAPTION, UI_COL_CAPTION);
  lv_obj_set_pos(range_label, 14, 43);

  const int offset = width ? 0 : 2;
  for (int side = 0; side < 2; ++side) {
    lv_obj_t* button = lv_btn_create(row);
    lv_obj_set_size(button, 56, 56);
    lv_obj_set_pos(button, side ? 370 : 222, 13);
    styleOutlineButton(button);
    lv_obj_add_event_cb(button, adjustCustom, LV_EVENT_CLICKED,
                        const_cast<Adjustment*>(&kAdjustments[offset + side]));
    lv_obj_add_event_cb(button, adjustCustom, LV_EVENT_LONG_PRESSED_REPEAT,
                        const_cast<Adjustment*>(&kAdjustments[offset + side]));
    lv_obj_t* symbol = addText(button, side ? "+" : "-", UI_FONT_TITLE, UI_COL_INK_2);
    lv_obj_center(symbol);
  }

  lv_obj_t* value = addText(row, "", UI_FONT_TITLE, UI_COL_ACCENT);
  lv_obj_set_width(value, 90);
  lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(value, 280, 30);
  if (width) custom_width_label = value;
  else custom_length_label = value;
}

void buildCustomScreen() {
  custom_width = config.media_width_mm;
  custom_length = config.media_length_mm;
  beginScreen(T(STR_PRINTER_CUSTOM_SIZE), Page::CUSTOM);
  addAdjustmentRow(62, STR_PRINTER_WIDTH_ACROSS, true);
  addAdjustmentRow(148, STR_PRINTER_FEED_LENGTH, false);

  lv_obj_t* save = lv_btn_create(screen);
  lv_obj_set_size(save, 280, 58);
  lv_obj_set_pos(save, 100, 246);
  styleOutlineButton(save);
  lv_obj_add_event_cb(save, [](lv_event_t*) {
    if (saveMedia(custom_width, custom_length)) main_pending = true;
  }, LV_EVENT_CLICKED, nullptr);
  custom_save_label = addText(save, "", UI_FONT_BODY, UI_COL_ACCENT);
  lv_obj_center(custom_save_label);
  updateCustomLabels();
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
}
}  // namespace

void requestPrinterSettingsScreen() { open_pending = true; }

void handlePrinterSettingsDeferredActions() {
  if (open_pending) {
    open_pending = false;
    if (!backendIsFilaMan()) return;
    hideAllOverlays();
    closeConnectionScreen();
    buildMainScreen();
    return;
  }
  if (back_pending) {
    back_pending = false;
    if (page == Page::CUSTOM) buildSizeScreen();
    else if (page == Page::SIZES || page == Page::MODELS) buildMainScreen();
    else {
      hidePrinterSettingsOverlays();
      closeConnectionScreen();
      buildConnectionScreen();
      hideAllOverlays();
      lv_obj_clear_flag(scr_connection, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }
  if (main_pending) { main_pending = false; buildMainScreen(); return; }
  if (models_pending) { models_pending = false; buildModelScreen(); return; }
  if (sizes_pending) { sizes_pending = false; buildSizeScreen(); return; }
  if (custom_pending) { custom_pending = false; buildCustomScreen(); return; }
  if (scan_pending && screen && page == Page::MAIN) {
    scan_pending = false;
    loadingOverlayShow(T(STR_PRINTER_SCANNING));
    device_count = labelPrinterScan(config, devices, 24, loadingOverlayTick);
    loadingOverlayHide();
    renderDevices();
    updateSavedAddress();
  }
}

void hidePrinterSettingsOverlays() {
  releaseScreen(&screen);
  clearPointers();
  device_count = 0;
  page = Page::MAIN;
  open_pending = back_pending = scan_pending = false;
  models_pending = sizes_pending = custom_pending = main_pending = false;
}
