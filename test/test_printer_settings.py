#!/usr/bin/env python3
"""Exercise printer settings callbacks, persistence failures, scan overlay, and model media limits."""
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)

    def header(name, body):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#pragma once\n' + body)

    header('lvgl.h', r'''
#include <vector>
#include <string>
#include <cstdarg>
#include <cstdio>
using lv_font_t = int;
extern std::vector<std::string> label_text;
struct lv_event_t { void* user_data=nullptr; };
using lv_event_cb_t = void (*)(lv_event_t*);
struct lv_obj_t { int x=0, y=0, width=0, height=0, bg=0; lv_obj_t* parent=nullptr; bool active=true; std::string text; lv_event_cb_t cb=nullptr; void* user_data=nullptr; };
extern std::vector<lv_obj_t*> objects;
inline lv_obj_t* lv_obj_create(lv_obj_t* p) { auto* o=new lv_obj_t; o->parent=p; objects.push_back(o); return o; }
inline lv_obj_t* lv_btn_create(lv_obj_t* p) { return lv_obj_create(p); }
inline lv_obj_t* lv_label_create(lv_obj_t* p) { return lv_obj_create(p); }
inline lv_obj_t* lv_canvas_create(lv_obj_t* p) { return lv_obj_create(p); }
inline lv_obj_t* lv_scr_act() { return nullptr; }
inline void lv_obj_set_size(lv_obj_t* o, int w, int h) { o->width=w; o->height=h; }
inline void lv_obj_set_width(lv_obj_t* o, int w) { o->width=w; }
inline void lv_obj_set_height(lv_obj_t*, int) {}
inline void lv_obj_set_pos(lv_obj_t* o, int x, int y) { o->x=x; o->y=y; }
inline void lv_obj_set_style_bg_color(lv_obj_t* o, int color, int) { o->bg=color; }
inline void lv_obj_set_style_radius(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_border_width(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_pad_all(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_pad_row(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_text_color(lv_obj_t*, int, int) {}
inline void lv_obj_set_style_text_font(lv_obj_t*, const int*, int) {}
inline void lv_obj_set_style_text_align(lv_obj_t*, int, int) {}
inline void lv_obj_set_flex_flow(lv_obj_t*, int) {}
inline void lv_obj_clear_flag(lv_obj_t*, int) {}
inline void lv_obj_add_flag(lv_obj_t*, int) {}
inline void lv_obj_clear_state(lv_obj_t*, int) {}
inline void lv_obj_add_state(lv_obj_t*, int) {}
inline void lv_obj_clean(lv_obj_t* p) { for (auto* o: objects) if (o->parent==p) { lv_obj_clean(o); o->active=false; } }
inline void lv_obj_remove_style_all(lv_obj_t*) {}
inline void lv_obj_center(lv_obj_t*) {}
inline void lv_obj_align(lv_obj_t*, int, int, int) {}
inline void lv_obj_add_event_cb(lv_obj_t* o, lv_event_cb_t cb, int, void* data) { o->cb=cb; o->user_data=data; }
inline void lv_label_set_text(lv_obj_t* o, const char* s) { o->text=s; label_text.push_back(s); }
inline void lv_label_set_text_fmt(lv_obj_t* o, const char* fmt, ...) { char b[256]; va_list a; va_start(a,fmt); vsnprintf(b,sizeof(b),fmt,a); va_end(a); lv_label_set_text(o,b); }
inline void* lv_event_get_user_data(lv_event_t* e) { return e->user_data; }
inline void lv_refr_now(void*) {}
inline void lv_canvas_set_buffer(lv_obj_t*, void*, int, int, int) {}
inline void lv_canvas_set_palette(lv_obj_t*, int, int) {}
inline int lv_color_white() { return 0xffffff; }
inline int lv_color_black() { return 0; }
inline void lv_label_set_long_mode(lv_obj_t*, int) {}
inline int lv_color_hex(int v) { return v; }
static const int lv_font_montserrat_ext_20=0;
#define LV_EVENT_CLICKED 1
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OBJ_FLAG_HIDDEN 2
#define LV_LABEL_LONG_DOT 1
#define LV_ALIGN_TOP_MID 1
#define LV_TEXT_ALIGN_CENTER 1
#define LV_FLEX_FLOW_COLUMN 1
#define LV_IMG_CF_INDEXED_1BIT 1
#define LV_EVENT_DELETE 2
#define LV_STATE_DISABLED 1
#define LV_SYMBOL_LEFT "<"

#define LV_EVENT_LONG_PRESSED_REPEAT 3
#define LV_ALIGN_LEFT_MID 2
#define LV_ALIGN_RIGHT_MID 3
#define LV_SYMBOL_OK "OK"
#define LV_SYMBOL_GPS "GPS"
#define LV_SYMBOL_RIGHT ">"
static const int lv_font_montserrat_ext_12=0, lv_font_montserrat_ext_14=0, lv_font_montserrat_ext_16=0, lv_font_montserrat_ext_18=0;
''')
    header('Arduino.h', '#include <stddef.h>\n#include <stdint.h>\n#include <string>\nclass String : public std::string { public: using std::string::string; bool isEmpty() const { return empty(); } };\n')
    header('app/app_state.h', '#include <lvgl.h>\nextern lv_obj_t* scr_connection;\n')
    header('hardware/sd_logger.h', '')
    header('services/backend.h', 'bool backendIsFilaMan();\n')
    header('services/prefs_store.h', '#include <Arduino.h>\nint prefsGetInt(const char*,int); bool prefsPutInt(const char*,int); String prefsGetString(const char*); bool prefsPutString(const char*,const char*);\n')
    header('ui/ui_common.h', '#include <lvgl.h>\nvoid releaseScreen(lv_obj_t**); inline bool lvPoolHasRoomForRow() { return true; } inline void styleOutlineButton(lv_obj_t*) {} inline void styleListPanel(lv_obj_t*) {} inline void styleListRow(lv_obj_t*, bool = false) {} inline lv_obj_t* buildOverlayScreen() { label_text.clear(); return lv_obj_create(nullptr); } inline void buildSubHeader(lv_obj_t* p,const char* title,lv_event_cb_t cb,const char* = nullptr) { lv_label_set_text(lv_label_create(p),title); auto* b=lv_btn_create(p); lv_obj_set_pos(b,12,8); lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr); }\n')
    strings = {
        'STR_PRINTER_SCAN': 'Scan Bluetooth',
        'STR_PRINTER_SCAN_EMPTY': 'No Bluetooth devices found',
        'STR_PRINTER_SELECT_DEVICE': 'Select a Bluetooth device',
        'STR_PRINTER_MODEL': 'Printer model',
        'STR_PRINTER_EXPERIMENTAL': 'Experimental',
        'STR_PRINTER_UNKNOWN_DEVICE': 'Unknown BLE device',
        'STR_PRINTER_CUSTOM_SIZE': 'Custom',
        'STR_PRINTER_CLEAR': 'Clear',
        'STR_ERR_SAVE': 'Error saving',
    }
    import re
    ids = list(dict.fromkeys(re.findall(r'\bSTR_\w+', (root / 'src/lang.h').read_text())))
    ids += [key for key in strings if key not in ids]
    cases = ''.join(f'case {key}: return "{value}";' for key, value in strings.items())
    header('lang.h', 'enum StringID {' + ','.join(ids) + '}; inline const char* T(StringID id) { switch(id) {' + cases + 'default: return "text";} }')
    source = r'''
#include <assert.h>
#include <cstring>
#include <Arduino.h>
#include <lvgl.h>
#include <lang.h>
#include "ui/printer_settings_screen.h"
#include "ui/info_popup.h"
#include "ui/loading_overlay.h"
#include "services/label_printer.h"
#include "services/phomemo_m220.h"
std::vector<lv_obj_t*> objects;
std::vector<std::string> label_text;
lv_obj_t* scr_connection=nullptr;
LabelPrinterConfig saved={LabelPrinterModel::M220,"Old printer","aa:bb:cc:dd:ee:ff",40,30};
int scan_calls=0, scan_result=24, loading_shown=0, loading_hidden=0, errors=0;
bool overlay=false, save_ok=true, in_callback=false, partial_failure=false;
int save_calls=0, load_calls=0;
const LabelPrinterProfile& labelPrinterProfile(LabelPrinterModel model) {
  static const LabelPrinterProfile m220={LabelPrinterModel::M220,"M220",40,30,20,75,10,150,576,648,false};
  static const LabelPrinterProfile m110={LabelPrinterModel::M110,"M110",40,30,20,48,10,150,384,384,true};
  return model==LabelPrinterModel::M110 ? m110 : m220;
}
LabelPrinterConfig labelPrinterLoadConfig() { ++load_calls; return saved; }
bool labelPrinterSaveConfig(const LabelPrinterConfig& value) {
  assert(!in_callback); // LVGL callbacks run while prefs writes only report queue acceptance.
  ++save_calls;
  if (partial_failure) {
    saved.model=value.model;
    strcpy(saved.address,value.address);
    saved.media_width_mm=value.media_width_mm;
    return false;
  }
  if (!save_ok) return false;
  saved=value;
  return true;
}
void loadingOverlayShow(const char*) { assert(!overlay); overlay=true; ++loading_shown; }
void loadingOverlayHide() { assert(overlay); overlay=false; ++loading_hidden; }
void loadingOverlayTick() { assert(overlay); }
size_t labelPrinterScan(const LabelPrinterConfig& config, LabelPrinterDevice* devices, size_t capacity, LabelPrinterProgressFn progress) {
  assert(overlay && capacity==24 && config.model==saved.model && progress==loadingOverlayTick);
  ++scan_calls; progress();
  for (int i=0; i<scan_result; ++i) {
    snprintf(devices[i].name,sizeof(devices[i].name),"Other %d",i);
    snprintf(devices[i].address,sizeof(devices[i].address),"11:22:33:44:55:%02x",i);
  }
  if (scan_result) { strcpy(devices[0].name,"Q123456789"); devices[1].name[0]=0; strcpy(devices[2].name,"M220 hint"); }
  return scan_result;
}
bool backendIsFilaMan() { return true; }
void hideAllOverlays() {}
void closeConnectionScreen() {}
void buildConnectionScreen() {}
void updateHeaderStatus() {}
void requestLabelPresetSettingsScreen() {}
void showInfoPopup(int title, int body, uint8_t tone) { assert(body==STR_ERR_SAVE && tone==INFO_WARN); ++errors; }
void releaseScreen(lv_obj_t** screen) { if (*screen) { lv_obj_clean(*screen); (*screen)->active=false; *screen=nullptr; } }
// Legacy service exists only to run the regression against the pre-change screen.
int prefsGetInt(const char* key,int) { return strstr(key,"_w") ? saved.media_width_mm : saved.media_length_mm; }
bool prefsPutInt(const char*,int) { return true; }
String prefsGetString(const char* key) { return String(strstr(key,"addr") ? saved.address : saved.name); }
bool prefsPutString(const char*,const char*) { return true; }
size_t phomemoM220Scan(M220Device*,size_t) { return 0; }
bool renderedText(const char* text) { for (const auto& s: label_text) if (s.find(text)!=std::string::npos) return true; return false; }
void tap(lv_obj_t* o) {
  assert(o && o->active && o->cb);
  const int before=save_calls;
  lv_event_t e; e.user_data=o->user_data;
  in_callback=true; o->cb(&e); in_callback=false;
  assert(save_calls==before);
}
std::vector<std::string> currentText() {
  std::vector<std::string> result;
  for (auto* o: objects) if (o->active) result.push_back(o->text);
  return result;
}
void failedSaveKeepsScreen() {
  const auto text=currentText();
  const auto count=objects.size();
  const int loads=load_calls, calls=save_calls, warnings=errors;
  handlePrinterSettingsDeferredActions();
  assert(save_calls==calls+1 && errors==warnings+1);
  assert(load_calls==loads && objects.size()==count && currentText()==text);
  handlePrinterSettingsDeferredActions();
  assert(save_calls==calls+1 && load_calls==loads && currentText()==text);
}
lv_obj_t* button(int width,int height) { for (auto* o: objects) if (o->active && o->cb && o->width==width && o->height==height) return o; assert(false); return nullptr; }
void tapModelButton() { tap(button(112,44)); }
void selectModel(LabelPrinterModel model) { for (auto* o: objects) if (o->active && o->cb && o->user_data==reinterpret_cast<void*>(static_cast<intptr_t>(model))) { tap(o); return; } assert(false); }
void tapScanButton() { tap(button(140,44)); }
void tapText(const char* text) { for (auto* o: objects) if (o->active && o->text.find(text)!=std::string::npos && o->parent && o->parent->cb) { tap(o->parent); return; } assert(false); }
void back() { for (auto* o: objects) if (o->active && o->x==12 && o->y==8 && o->cb) { tap(o); handlePrinterSettingsDeferredActions(); return; } assert(false); }
void chooseModel(LabelPrinterModel model) { tapModelButton(); handlePrinterSettingsDeferredActions(); selectModel(model); handlePrinterSettingsDeferredActions(); }
int main() {
  requestPrinterSettingsScreen();
  handlePrinterSettingsDeferredActions();
  assert(renderedText("M220"));
  tap(button(456,68)); handlePrinterSettingsDeferredActions();
  tapText("50 x 25 mm"); handlePrinterSettingsDeferredActions();
  assert(saved.media_width_mm==50 && saved.media_length_mm==25);
  tapModelButton(); handlePrinterSettingsDeferredActions();
  assert(renderedText("Experimental"));
  selectModel(LabelPrinterModel::M110); handlePrinterSettingsDeferredActions();
  assert(saved.model==LabelPrinterModel::M110);
  assert(renderedText("Experimental"));
  assert(saved.media_width_mm==40 && saved.media_length_mm==30);
  assert(!strcmp(saved.address,"aa:bb:cc:dd:ee:ff"));
  tapScanButton(); handlePrinterSettingsDeferredActions();
  assert(scan_calls==1 && loading_shown==1 && loading_hidden==1);
  assert(renderedText("Q123456789") && renderedText("Unknown BLE device") && renderedText("Other 23"));
  tapText("M220 hint"); handlePrinterSettingsDeferredActions();
  assert(saved.model==LabelPrinterModel::M110 && !strcmp(saved.address,"11:22:33:44:55:02"));
  save_ok=false; tapText("Q123456789"); failedSaveKeepsScreen(); assert(errors==1 && !strcmp(saved.address,"11:22:33:44:55:02"));
  tapText("Clear"); failedSaveKeepsScreen(); assert(errors==2 && saved.address[0]);
  save_ok=true; tapText("Unknown BLE device"); handlePrinterSettingsDeferredActions(); assert(!saved.name[0] && !strcmp(saved.address,"11:22:33:44:55:01"));
  scan_result=0; label_text.clear(); tapScanButton(); handlePrinterSettingsDeferredActions();
  assert(loading_shown==2 && loading_hidden==2 && renderedText("No Bluetooth devices found"));
  tap(button(456,68)); handlePrinterSettingsDeferredActions();
  assert(!renderedText("50 x 25 mm") && renderedText("40 x 60 mm"));
  save_ok=false; tapText("40 x 60 mm"); failedSaveKeepsScreen();
  assert(saved.media_length_mm==30 && errors==3);
  save_ok=true; tapText("Custom"); handlePrinterSettingsDeferredActions();
  assert(renderedText("20-48 mm"));
  for (auto* o: objects) if (o->active && o->cb && o->x==370 && o->parent->y==62) for (int i=0;i<20;++i) tap(o);
  save_ok=false; tap(button(280,58)); failedSaveKeepsScreen();
  assert(saved.media_width_mm==40);
  save_ok=true; tap(button(280,58)); handlePrinterSettingsDeferredActions(); assert(saved.media_width_mm==48);
  chooseModel(LabelPrinterModel::M220); assert(saved.media_width_mm==48);
  save_ok=false; tapModelButton(); handlePrinterSettingsDeferredActions();
  selectModel(LabelPrinterModel::M110); failedSaveKeepsScreen();
  assert(saved.model==LabelPrinterModel::M220 && errors==5);
  back(); save_ok=true; tapText("Clear"); handlePrinterSettingsDeferredActions(); assert(!saved.name[0] && !saved.address[0] && saved.model==LabelPrinterModel::M220);
  // A partial NVS write must not rebuild from mixed persisted values on failure or Back.
  for (int path=0; path<5; ++path) {
    hidePrinterSettingsOverlays();
    saved={LabelPrinterModel::M220,"Old printer","aa:bb:cc:dd:ee:ff",50,25};
    scan_result=24;
    requestPrinterSettingsScreen(); handlePrinterSettingsDeferredActions();
    const int loads=load_calls;
    if (path==0) { tapScanButton(); handlePrinterSettingsDeferredActions(); tapText("Q123456789"); }
    if (path==1) tapText("Clear");
    if (path==2) { tapModelButton(); handlePrinterSettingsDeferredActions(); selectModel(LabelPrinterModel::M110); }
    if (path>=3) {
      tap(button(456,68)); handlePrinterSettingsDeferredActions();
      if (path==3) tapText("40 x 60 mm");
      else {
        tapText("Custom"); handlePrinterSettingsDeferredActions();
        for (auto* o: objects) if (o->active && o->cb && o->x==370 && o->parent->y==62) tap(o);
        tap(button(280,58));
      }
    }
    partial_failure=true;
    failedSaveKeepsScreen();
    partial_failure=false;
    if (path>=2) back();
    if (path==4) back();
    assert(load_calls==loads);
    bool old_name=false, old_size=false, old_model=false;
    for (const auto& text: currentText()) {
      old_name |= text=="Old printer";
      old_size |= text=="50 x 25 mm";
      old_model |= text=="M220";
    }
    assert(old_name && old_size && old_model);
    // The next successful edit must be based on the last committed screen config.
    tapText("Clear"); handlePrinterSettingsDeferredActions();
    assert(saved.model==LabelPrinterModel::M220 && saved.media_width_mm==50 && saved.media_length_mm==25);
    assert(!saved.name[0] && !saved.address[0]);
  }
  tapModelButton(); handlePrinterSettingsDeferredActions();
  selectModel(LabelPrinterModel::M110);
  const int calls=save_calls;
  hidePrinterSettingsOverlays(); handlePrinterSettingsDeferredActions();
  assert(save_calls==calls); // Closing cancels queued settings writes.
}
'''
    result = subprocess.run(['g++', '-std=c++11', f'-I{tmp}', f'-I{root / "src"}', '-x', 'c++', '-', os.environ.get('PRINTER_SETTINGS_SOURCE', str(root / 'src/ui/printer_settings_screen.cpp')), '-o', str(tmp / 'check')], input=source, text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(tmp / 'check')], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    print('printer settings UI checks passed')
