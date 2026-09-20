#!/usr/bin/env python3
"""Drive the production LVGL callbacks and deferred handler with a host UI shim."""
from ui_shim import LVGL_SHIM
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
    header('Arduino.h', '#include <stddef.h>\n#include <stdint.h>\n#include <string>\nclass String : public std::string { public: using std::string::string; bool isEmpty() const { return empty(); } };\ninline void delay(int) {}\nstruct ESPClass { unsigned getFreeHeap() const { return 1; } }; extern ESPClass ESP;\nstruct SerialClass { template<class... T> void printf(const char*, T...) {} }; extern SerialClass Serial;\n')
    header('WiFi.h', 'struct WiFiClass { void disconnect(bool) {} }; extern WiFiClass WiFi;\n')
    header('esp_heap_caps.h', '#include <assert.h>\n#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 0\nextern bool thumbnail_fail; extern int loading_depth; inline void* heap_caps_malloc(size_t n, int) { assert(loading_depth==1); return thumbnail_fail ? nullptr : malloc(n); }\n')
    header('lvgl.h', LVGL_SHIM)
    header('services/filaman_api.h', '''#include <stddef.h>\n#include <stdint.h>\nstruct FilaManLabelPreset { int id; char name[64]; };\nint filamanListLabelPresets(const char*,const char*,FilaManLabelPreset*,size_t,size_t*);\nint filamanRequestLabelPrint(const char*,const char*,int,int,int*,uint32_t=8000);\n''')
    header('services/http_progress.h', '#include <stddef.h>\nextern bool progress_active; struct HttpStallTime {}; struct HttpStall { explicit HttpStall(void (*)(size_t)) { progress_active=true; } ~HttpStall() { progress_active=false; } };\n')
    header('ui/loading_overlay.h', 'void loadingOverlayShow(const char*); void loadingOverlayHide(); void loadingOverlayTick(); void loadingOverlayProgress(size_t);\n')
    header('app/app_state.h', 'extern bool sm_found; extern int sm_id; extern char cfg_wifi_ssid[33]; extern char cfg_wifi_password[65];\n')
    header('lang.h', '''enum { STR_LABEL_DEFAULT, STR_LABEL_NO_WIFI, STR_LABEL_LOADING, STR_LABEL_LOAD_FAIL, STR_LABEL_PRESET_REMOVED, STR_LABEL_NONE, STR_LABEL_PRESET_TITLE, STR_LABEL_REFRESH, STR_LABEL_PC_PENDING, STR_LABEL_PC_OPEN, STR_LABEL_PC_QUEUED, STR_LABEL_PC_KEY, STR_LABEL_PC_SCOPE, STR_LABEL_PC_MISSING, STR_LABEL_PC_INVALID, STR_LABEL_PC_FAILED, STR_LABEL_PREVIEW, STR_LABEL_CHANGE_PRESET, STR_LABEL_PRINTER_PRINT, STR_LABEL_PRINTER_SELECT, STR_LABEL_PRINTER_MEDIA, STR_LABEL_PRINTER_FETCH, STR_LABEL_PRINTER_SEND, STR_LABEL_PRINTER_SENT, STR_LABEL_PRINTER_FAILED, STR_LABEL_PRINTER_NO_PSRAM };\nextern int status_id; inline const char* T(int id) { status_id=id; return id==STR_LABEL_PRINTER_SEND ? "Sending to %s..." : "text"; }\n''')
    header('services/backend.h', 'bool backendIsFilaMan(); const char* backendBaseUrl(); const char* filamanApiKey();\n')
    header('services/prefs_store.h', '#include <Arduino.h>\nint prefsGetInt(const char*,int); bool prefsPutInt(const char*,int); String prefsGetString(const char*); bool prefsPutString(const char*,const char*);\n')
    header('services/wifi_manager.h', 'bool wifiManagerIsConnected(); void wifiManagerBegin(const char*, const char*);\n')
    header('ui/navigation.h', 'void hideAllOverlays();\n')
    header('ui/more_info_screen.h', 'void showMoreInfoScreen();\n')
    header('ui/label_preset_selection.h', '#include <lvgl.h>\nvoid labelPresetRowCb(lv_event_t*);\n')
    header('ui/printer_settings_screen.h', 'void requestPrinterSettingsScreen();\n')
    header('ui/ui_common.h', '#include <lvgl.h>\nvoid releaseScreen(lv_obj_t**); inline bool lvPoolHasRoomForRow() { return true; } inline void styleOutlineButton(lv_obj_t*) {} inline void styleListPanel(lv_obj_t*) {} inline void styleListRow(lv_obj_t*, bool = false) {} inline lv_obj_t* buildOverlayScreen() { return lv_obj_create(nullptr); } inline void buildSubHeader(lv_obj_t* p,const char*,lv_event_cb_t cb,const char* = nullptr) { auto* b=lv_btn_create(p); lv_obj_set_pos(b,12,8); lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr); }\n')
    source = r'''
#include <assert.h>
#include <Arduino.h>
#include <WiFi.h>
#include <lang.h>
#include <vector>
#include <lvgl.h>
#include "ui/label_print_screen.h"
#include "services/filaman_api.h"
#include "services/filaman_labels.h"
#include "services/label_printer.h"
#include <cstring>
std::vector<lv_obj_t*> objects;
WiFiClass WiFi;
ESPClass ESP; SerialClass Serial;
bool sm_found=true; int sm_id=123;
char cfg_wifi_ssid[33]="wifi", cfg_wifi_password[65]="pass";
int preset=7, posts=0, sent_spool=0, sent_preset=0, closed=0, status_id=0;
int response=201, fetch_response=-1, preset_fetches=0, preset_response=200, content_delta=0;
int loading_depth=0, loading_shown=0, loading_hidden=0, print_calls=0;
bool progress_active=false, thumbnail_fail=false, print_result=true;
std::string loading_message;
LabelPrinterConfig config{LabelPrinterModel::M220,"Saved","aa:bb:cc:dd:ee:ff",40,30};
LabelPrinterConfig labelPrinterLoadConfig() { return config; }
bool labelPrinterConfigured(const LabelPrinterConfig& c) { return c.address[0]; }
const LabelPrinterProfile& labelPrinterProfile(LabelPrinterModel m) {
  static const LabelPrinterProfile m220{LabelPrinterModel::M220,"M220"};
  static const LabelPrinterProfile m110{LabelPrinterModel::M110,"M110"};
  return m==LabelPrinterModel::M220 ? m220 : m110;
}
uint16_t labelPrinterRasterWidth(LabelPrinterModel m,uint16_t) { return m==LabelPrinterModel::M220 ? 576 : 384; }
bool labelPrinterRasterFits(LabelPrinterModel m,const LabelRaster& r,uint16_t w,uint16_t h) {
  return r.width==labelPrinterRasterWidth(m,w) && r.content_width==w*8 && r.height==h*8;
}
void loadingOverlayShow(const char* message) { assert(loading_depth++==0); ++loading_shown; loading_message=message; }
void loadingOverlayHide() { assert(loading_depth--==1); ++loading_hidden; }
void loadingOverlayTick() { assert(loading_depth==1); }
void loadingOverlayProgress(size_t) { loadingOverlayTick(); }
void balanced(int before) { assert(loading_shown==before+1 && loading_hidden==loading_shown && loading_depth==0 && !progress_active); }
bool labelPrinterPrint(const LabelPrinterConfig& c,const LabelRaster&,char* error,size_t n,LabelPrinterProgressFn progress) {
  assert(loading_depth==1 && c.model==config.model && !strcmp(c.address,config.address));
  assert(progress); progress(); ++print_calls; snprintf(error,n,"write failed"); return print_result;
}
bool wifi=true;
bool backendIsFilaMan() { return true; }
const char* backendBaseUrl() { return "http://fila"; }
const char* filamanApiKey() { return "uak.key"; }
bool wifiManagerIsConnected() { return wifi; }
void wifiManagerBegin(const char*,const char*) {}
int prefsGetInt(const char*,int) { return preset; }
bool prefsPutInt(const char*,int v) { preset=v; return true; }
String prefsGetString(const char*) { return String("aa:bb:cc:dd:ee:ff"); }
bool prefsPutString(const char*,const char*) { return true; }
int filamanListLabelPresets(const char*,const char*,FilaManLabelPreset* out,size_t,size_t* count) { assert(loading_depth==1 && progress_active); ++preset_fetches; out[0].id=7; out[0].name[0]='A'; out[0].name[1]=0; *count=1; return preset_response; }
int filamanRequestLabelPrint(const char*,const char*,int spool,int chosen,int* id,uint32_t) { assert(loading_depth==1 && progress_active); ++posts; sent_spool=spool; sent_preset=chosen; *id=42; return response; }
int filamanFetchMonoLabel(const char*,const char*,int,int,uint16_t width,const char* orientation,LabelRaster* out,uint32_t) {
  assert(loading_depth==1 && progress_active);
  assert(width==(config.model==LabelPrinterModel::M220 ? 576 : 384));
  assert(!strcmp(orientation,config.media_width_mm>=config.media_length_mm ? "landscape" : "portrait"));
  static uint8_t pixels[72*400]{};
  if (fetch_response==200) *out={width,uint16_t(config.media_length_mm*8),uint16_t(width/8),pixels,size_t(width/8*config.media_length_mm*8),uint16_t(config.media_width_mm*8+content_delta),false}; return fetch_response; }
void filamanFreeLabel(LabelRaster* r) { *r={}; }
void hideAllOverlays() {}
void releaseScreen(lv_obj_t** screen) { if (*screen) { ++closed; *screen=nullptr; } }
void showMoreInfoScreen() {}
void requestPrinterSettingsScreen() {}
void tap(int x,int y) { for (auto it=objects.rbegin();it!=objects.rend();++it) if (auto* o=*it) if (o->x==x && o->y==y && o->cb) { assert(!o->disabled); lv_event_t e; e.user_data=o->user_data; o->cb(&e); return; } assert(false); }
int main() {
  fetch_response=200;
  requestLabelPreviewScreen(123); handleLabelPrintDeferredActions();
  tap(342,8); handleLabelPrintDeferredActions();
  lv_obj_t* default_row=nullptr;
  for (auto* o: objects) if (o->width==392 && o->cb && o->user_data==nullptr) default_row=o;
  assert(default_row);
  lv_event_t select; select.user_data=nullptr; default_row->cb(&select);
  assert(preset==0);
  size_t before_refresh=objects.size();
  handleLabelPrintDeferredActions();
  bool new_default=false, new_saved=false;
  for (size_t i=before_refresh;i<objects.size();++i) {
    auto* o=objects[i];
    if (o->width!=392 || !o->cb) continue;
    if (o->user_data==nullptr) new_default=true;
    if (o->user_data==(void*)7) new_saved=true;
  }
  assert(new_default && new_saved);
  preset=7;
  tap(12,8); handleLabelPrintDeferredActions();
  handleLabelPrintDeferredActions();
  assert(posts==0);
  preset=7; tap(45,261); tap(45,261); preset=8;
  handleLabelPrintDeferredActions();
  assert(posts==1 && sent_spool==123 && sent_preset==7);
  tap(45,261); hideLabelPrintOverlays(); handleLabelPrintDeferredActions();
  assert(posts==1);
  requestLabelPreviewScreen(123); handleLabelPrintDeferredActions();
  wifi=false; tap(45,261); handleLabelPrintDeferredActions();
  assert(status_id==STR_LABEL_NO_WIFI && posts==1);
  wifi=true;
  const int codes[]={401,403,404,422};
  const int statuses[]={STR_LABEL_PC_KEY,STR_LABEL_PC_SCOPE,STR_LABEL_PC_MISSING,STR_LABEL_PC_INVALID};
  for (int i=0;i<4;++i) {
    response=codes[i]; int before=preset_fetches;
    tap(45,261); handleLabelPrintDeferredActions();
    assert(status_id==statuses[i]);
    assert(preset_fetches==before);
  }
  fetch_response=-3; requestLabelPreviewScreen(123); handleLabelPrintDeferredActions();
  assert(status_id==STR_LABEL_PRINTER_NO_PSRAM);
  assert(loading_shown==loading_hidden && loading_depth==0);
  fetch_response=200;
  for (auto model: {LabelPrinterModel::M220,LabelPrinterModel::M110}) {
    config.model=model;
    config.media_width_mm=30; config.media_length_mm=40;
    int before=loading_shown;
    requestLabelPreviewScreen(123); handleLabelPrintDeferredActions(); balanced(before);
    before=loading_shown;
    print_result=false; tap(255,261); handleLabelPrintDeferredActions(); balanced(before);
    assert(loading_message==(model==LabelPrinterModel::M220 ? "Sending to M220..." : "Sending to M110..."));
    before=loading_shown;
    print_result=true; tap(255,261); handleLabelPrintDeferredActions(); balanced(before);
    const int calls=print_calls;
    config.media_length_mm=30;
    before=loading_shown; tap(255,261); handleLabelPrintDeferredActions();
    assert(print_calls==calls && loading_shown==before && status_id==STR_LABEL_PRINTER_MEDIA);
    config.media_length_mm=40;
    config.model=model==LabelPrinterModel::M220 ? LabelPrinterModel::M110 : LabelPrinterModel::M220;
    tap(255,261); handleLabelPrintDeferredActions();
    assert(print_calls==calls && loading_shown==before && status_id==STR_LABEL_PRINTER_MEDIA);
    config.model=model; config.address[0]=0;
    tap(255,261); handleLabelPrintDeferredActions();
    assert(print_calls==calls && loading_shown==before && status_id==STR_LABEL_PRINTER_SELECT);
    strcpy(config.address,"aa:bb:cc:dd:ee:ff");
  }
  int before=loading_shown;
  content_delta=-1;
  requestLabelPreviewScreen(123); handleLabelPrintDeferredActions(); balanced(before);
  assert(status_id==STR_LABEL_PRINTER_MEDIA);
  for (auto it=objects.rbegin();it!=objects.rend();++it) {
    auto* o=*it;
    if (o->x==255 && o->y==261) { assert(o->disabled); break; }
  }
  content_delta=0; before=loading_shown;
  thumbnail_fail=true; requestLabelPreviewScreen(123); handleLabelPrintDeferredActions(); balanced(before);
  assert(status_id==STR_LABEL_PRINTER_NO_PSRAM);
  thumbnail_fail=false;
  for (int code: {FILAMAN_LABEL_NO_PSRAM,500,401,403}) {
    before=loading_shown; fetch_response=code;
    requestLabelPreviewScreen(123); handleLabelPrintDeferredActions(); balanced(before);
  }
  fetch_response=200; requestLabelPreviewScreen(123); handleLabelPrintDeferredActions();
  before=loading_shown; response=500; tap(45,261); handleLabelPrintDeferredActions(); balanced(before);
  before=loading_shown; preset_response=500;
  requestLabelPresetSettingsScreen(); handleLabelPrintDeferredActions(); balanced(before);
  before=loading_shown; wifi=false;
  requestLabelPreviewScreen(123); handleLabelPrintDeferredActions();
  requestLabelPresetSettingsScreen(); handleLabelPrintDeferredActions();
  assert(loading_shown==before && loading_depth==0);
}
'''
    result = subprocess.run(['g++','-std=c++11',f'-I{tmp}',f'-I{root / "src"}', f'-I{root / "src/ui"}','-x','c++','-',str(root/'src/ui/label_preset_selection.cpp'),os.environ.get('FILAMAN_LABEL_SCREEN_SOURCE', str(root/'src/ui/label_print_screen.cpp')),'-o',str(tmp/'check')],input=source,text=True,capture_output=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(tmp/'check')],capture_output=True,text=True)
    assert result.returncode == 0, result.stderr
