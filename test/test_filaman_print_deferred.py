#!/usr/bin/env python3
"""Drive the production LVGL callbacks and deferred handler with a host UI shim."""
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    def header(name, body):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#pragma once\n' + body)
    header('Arduino.h', '#include <stddef.h>\n#include <stdint.h>\n#include <string>\nclass String : public std::string { public: using std::string::string; bool isEmpty() const { return empty(); } };\n')
    header('lvgl.h', r'''
#include <vector>
struct lv_event_t { void* user_data=nullptr; };
using lv_event_cb_t = void (*)(lv_event_t*);
struct lv_obj_t { int x=0, y=0, width=0, bg=0; lv_event_cb_t cb=nullptr; void* user_data=nullptr; };
extern std::vector<lv_obj_t*> objects;
inline lv_obj_t* lv_obj_create(lv_obj_t*) { auto* o=new lv_obj_t; objects.push_back(o); return o; }
inline lv_obj_t* lv_btn_create(lv_obj_t* p) { return lv_obj_create(p); }
inline lv_obj_t* lv_label_create(lv_obj_t* p) { return lv_obj_create(p); }
inline lv_obj_t* lv_scr_act() { return nullptr; }
inline void lv_obj_set_size(lv_obj_t*, int, int) {}
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
inline void lv_obj_clean(lv_obj_t*) {}
inline void lv_obj_center(lv_obj_t*) {}
inline void lv_obj_align(lv_obj_t*, int, int, int) {}
inline void lv_obj_add_event_cb(lv_obj_t* o, lv_event_cb_t cb, int, void* data) { o->cb=cb; o->user_data=data; }
inline void lv_label_set_text(lv_obj_t*, const char*) {}
inline void lv_label_set_text_fmt(lv_obj_t*, const char*, ...) {}
inline void* lv_event_get_user_data(lv_event_t* e) { return e->user_data; }
inline void lv_label_set_long_mode(lv_obj_t*, int) {}
inline int lv_color_hex(int v) { return v; }
static const int lv_font_montserrat_ext_20=0;
#define LV_EVENT_CLICKED 1
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_LABEL_LONG_DOT 1
#define LV_ALIGN_TOP_MID 1
#define LV_TEXT_ALIGN_CENTER 1
#define LV_FLEX_FLOW_COLUMN 1
#define LV_SYMBOL_LEFT "<"
''')
    header('services/filaman_api.h', '''#include <stddef.h>\n#include <stdint.h>\nstruct FilaManLabelPreset { int id; char name[64]; };\nint filamanListLabelPresets(const char*,const char*,FilaManLabelPreset*,size_t,size_t*);\nint filamanRequestLabelPrint(const char*,const char*,int,int,int*,uint32_t=8000);\n''')
    header('services/http_progress.h', 'struct HttpStallTime {};\n')
    header('app/app_state.h', 'extern bool sm_found; extern int sm_id;\n')
    header('lang.h', '''enum { STR_LABEL_DEFAULT, STR_LABEL_NO_WIFI, STR_LABEL_LOADING, STR_LABEL_LOAD_FAIL, STR_LABEL_PRESET_REMOVED, STR_LABEL_NONE, STR_LABEL_PRESET_TITLE, STR_LABEL_REFRESH, STR_LABEL_PC_PENDING, STR_LABEL_PC_OPEN, STR_LABEL_PC_QUEUED, STR_LABEL_PC_KEY, STR_LABEL_PC_SCOPE, STR_LABEL_PC_MISSING, STR_LABEL_PC_INVALID, STR_LABEL_PC_FAILED, STR_LABEL_M220_SCAN, STR_LABEL_M220_PRINT, STR_LABEL_M220_NONE, STR_LABEL_M220_SELECT, STR_LABEL_M220_FETCH, STR_LABEL_M220_SEND, STR_LABEL_M220_SENT, STR_LABEL_M220_FAILED, STR_LABEL_M220_NO_PSRAM };\nextern int status_id; inline const char* T(int id) { status_id=id; return "text"; }\n''')
    header('services/backend.h', 'bool backendIsFilaMan(); const char* backendBaseUrl(); const char* filamanApiKey();\n')
    header('services/prefs_store.h', '#include <Arduino.h>\nint prefsGetInt(const char*,int); bool prefsPutInt(const char*,int); String prefsGetString(const char*); bool prefsPutString(const char*,const char*);\n')
    header('services/wifi_manager.h', 'bool wifiManagerIsConnected();\n')
    header('ui/navigation.h', 'void hideAllOverlays();\n')
    header('ui/more_info_screen.h', 'void showMoreInfoScreen();\n')
    header('ui/label_preset_selection.h', '#include <lvgl.h>\nvoid labelPresetRowCb(lv_event_t*);\n')
    header('ui/printer_settings_screen.h', 'void requestPrinterSettingsScreen();\n')
    header('ui/ui_common.h', '#include <lvgl.h>\nvoid releaseScreen(lv_obj_t**); inline bool lvPoolHasRoomForRow() { return true; } inline void styleOutlineButton(lv_obj_t*) {} inline void styleListPanel(lv_obj_t*) {} inline void styleListRow(lv_obj_t*, bool = false) {} inline lv_obj_t* buildOverlayScreen() { return lv_obj_create(nullptr); } inline void buildSubHeader(lv_obj_t* p,const char*,lv_event_cb_t cb,const char* = nullptr) { auto* b=lv_btn_create(p); lv_obj_set_pos(b,12,8); lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr); }\n')
    source = r'''
#include <assert.h>
#include <Arduino.h>
#include <lang.h>
#include <vector>
#include <lvgl.h>
#include "ui/label_print_screen.h"
#include "services/filaman_api.h"
#include "services/filaman_labels.h"
#include "services/phomemo_m220.h"
std::vector<lv_obj_t*> objects;
bool sm_found=true; int sm_id=123;
int preset=7, posts=0, sent_spool=0, sent_preset=0, closed=0, status_id=0;
int response=201, fetch_response=-1, preset_fetches=0;
bool wifi=true;
bool backendIsFilaMan() { return true; }
const char* backendBaseUrl() { return "http://fila"; }
const char* filamanApiKey() { return "uak.key"; }
bool wifiManagerIsConnected() { return wifi; }
int prefsGetInt(const char*,int) { return preset; }
bool prefsPutInt(const char*,int v) { preset=v; return true; }
String prefsGetString(const char*) { return String("aa:bb:cc:dd:ee:ff"); }
bool prefsPutString(const char*,const char*) { return true; }
int filamanListLabelPresets(const char*,const char*,FilaManLabelPreset* out,size_t,size_t* count) { ++preset_fetches; out[0].id=7; out[0].name[0]='A'; out[0].name[1]=0; *count=1; return 200; }
int filamanRequestLabelPrint(const char*,const char*,int spool,int chosen,int* id,uint32_t) { ++posts; sent_spool=spool; sent_preset=chosen; *id=42; return response; }
int filamanFetchMonoLabel(const char*,const char*,int,int,uint16_t,const char*,LabelRaster* out,uint32_t) { static uint8_t pixels[40*240]{}; if (fetch_response==200) *out={320,240,40,pixels,sizeof(pixels),320,false}; return fetch_response; }
void filamanFreeLabel(LabelRaster*) {}
size_t phomemoM220Scan(M220Device*,size_t) { return 0; }
bool phomemoM220Print(const char*,const LabelRaster&,char*,size_t) { return false; }
void hideAllOverlays() {}
void releaseScreen(lv_obj_t** screen) { if (*screen) { ++closed; *screen=nullptr; } }
void showMoreInfoScreen() {}
void tap(int x,int y) { for (auto* o: objects) if (o->x==x && o->y==y && o->cb) { lv_event_t e; e.user_data=o->user_data; o->cb(&e); return; } assert(false); }
int main() {
  requestLabelPresetScreen(123); handleLabelPrintDeferredActions();
  lv_obj_t* default_row=nullptr;
  for (auto* o: objects) if (o->width==392 && o->cb && o->user_data==nullptr) default_row=o;
  assert(default_row && default_row->bg==0x102035);
  lv_event_t select; select.user_data=nullptr; default_row->cb(&select);
  assert(preset==0 && default_row->bg==0x102035);
  size_t before_refresh=objects.size();
  handleLabelPrintDeferredActions();
  assert(default_row->bg==0x102035);
  bool new_default=false, new_saved=false;
  for (size_t i=before_refresh;i<objects.size();++i) {
    auto* o=objects[i];
    if (o->width!=392 || !o->cb) continue;
    if (o->user_data==nullptr && o->bg==0x174f46) new_default=true;
    if (o->user_data==(void*)7 && o->bg==0x102035) new_saved=true;
  }
  assert(new_default && new_saved);
  preset=7;
  tap(45,252); tap(45,252); preset=8;
  tap(12,8); handleLabelPrintDeferredActions();
  assert(posts==0 && closed==1);
  requestLabelPresetScreen(123); handleLabelPrintDeferredActions();
  preset=7; tap(45,252); tap(45,252); preset=8;
  handleLabelPrintDeferredActions();
  assert(posts==1 && sent_spool==123 && sent_preset==7);
  tap(45,252); hideLabelPrintOverlays(); handleLabelPrintDeferredActions();
  assert(posts==1 && closed==2);
  requestLabelPresetScreen(123); handleLabelPrintDeferredActions();
  wifi=false; tap(45,252); handleLabelPrintDeferredActions();
  assert(status_id==STR_LABEL_NO_WIFI && posts==1);
  wifi=true;
  const int codes[]={401,403,404,422};
  const int statuses[]={STR_LABEL_PC_KEY,STR_LABEL_PC_SCOPE,STR_LABEL_PC_MISSING,STR_LABEL_PC_INVALID};
  for (int i=0;i<4;++i) {
    response=codes[i]; int before=preset_fetches;
    tap(45,252); handleLabelPrintDeferredActions();
    assert(status_id==statuses[i]);
    if (codes[i]==404) assert(preset_fetches==before+1);
  }
  fetch_response=-3; tap(255,252); handleLabelPrintDeferredActions();
  assert(status_id==STR_LABEL_M220_NO_PSRAM);
}
'''
    result = subprocess.run(['g++','-std=c++11',f'-I{tmp}',f'-I{root / "src"}', f'-I{root / "src/ui"}','-x','c++','-',str(root/'src/ui/label_preset_selection.cpp'),os.environ.get('FILAMAN_LABEL_SCREEN_SOURCE', str(root/'src/ui/label_print_screen.cpp')),'-o',str(tmp/'check')],input=source,text=True,capture_output=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(tmp/'check')],capture_output=True,text=True)
    assert result.returncode == 0, result.stderr
