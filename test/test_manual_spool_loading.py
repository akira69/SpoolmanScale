#!/usr/bin/env python3
"""Exercise manual spool loading and opening cleanup against the production UI."""
from ui_shim import LVGL_SHIM
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
    header('lvgl.h', LVGL_SHIM + '\ninline void lv_obj_set_scroll_dir(lv_obj_t*, int) {}\ninline void lv_obj_del(lv_obj_t*) {}\n#define LV_DIR_VER 1\n#define LV_SYMBOL_RIGHT ">"\n')

    header('lang.h', 'enum { STR_SPOOLS_TITLE, STR_LABEL_NO_WIFI, STR_SPOOLS_LOADING, STR_SPOOLS_LOAD_FAIL, STR_SPOOLS_LOW_MEM, STR_SPOOLS_EMPTY, STR_SPOOLS_PAGE_FMT, STR_SPOOLS_REMOVE_TAG, STR_SPOOLS_OPENING, STR_SPOOLS_OPEN_FAIL }; const char* T(int);\n')
    header('Arduino.h', '#include <stddef.h>\n#include <stdint.h>\n')
    header('esp_heap_caps.h', '#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 0\ninline void* heap_caps_malloc(size_t n,int) { return malloc(n); }\ninline void heap_caps_free(void* p) { free(p); }\ninline void* heap_caps_realloc(void* p,size_t n,int) { return realloc(p,n); }\n')
    header('app/app_state.h', 'extern bool tag_present, sm_found; extern int sm_id;\n')
    header('app/app_loop.h', 'void cancelPendingNfcClear(); void cancelRemoteTaglessAdoption();\n')
    header('hardware/sd_logger.h', '')
    header('services/backend.h', 'bool backendIsFilaMan(); const char* backendBaseUrl(); const char* filamanApiKey();\n')
    header('services/list_limits.h', 'extern int spool_list_limit;\n')
    header('services/filaman_api.h', '#include <ArduinoJson.h>\nint filamanGetSpoolPageJson(const char*,const char*,int,int,JsonDocument&,int*);\n')
    header('services/http_progress.h', '#include <stddef.h>\nextern bool progress_active; struct HttpStallTime {}; struct HttpStall { explicit HttpStall(void (*)(size_t)) { progress_active=true; } ~HttpStall() { progress_active=false; } };\n')
    header('services/wifi_manager.h', 'bool wifiManagerIsConnected();\n')
    header('ui/loading_overlay.h', 'void loadingOverlayShow(const char*); void loadingOverlayHide(); void loadingOverlayProgress(size_t);\n')
    header('ui/more_info_screen.h', 'void showMoreInfoScreen();\n')
    header('ui/navigation.h', 'void hideAllOverlays(); void showMainScreen();\n')
    header('ui/spoolman_lookup.h', 'bool querySpoolmanById(int);\n')
    header('ui/tag_display.h', 'void clearTagDisplay();\n')
    header('ui/ui_common.h', '#include <lvgl.h>\nvoid releaseScreen(lv_obj_t**); inline bool lvPoolHasRoomForRow() { return true; } inline void styleOutlineButton(lv_obj_t*) {} inline void styleListPanel(lv_obj_t*) {} inline void styleListRow(lv_obj_t*) {} inline lv_obj_t* buildOverlayScreen() { return lv_obj_create(nullptr); } inline void buildSubHeader(lv_obj_t*,const char*,lv_event_cb_t) {}\n')
    source = r"""
#include <assert.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include "ui/manual_spool_screen.h"
#include "ui/main_screen_helpers.h"
std::vector<lv_obj_t*> objects;
bool wifi=true, tag_present=false, sm_found=false, lookup_ok=true, progress_active=false;
int sm_id=0, spool_list_limit=20, response=200, pages=0, lookups=0, opened=0, adoption_resets=0;
int loading_depth=0, loading_shown=0, loading_hidden=0;
const char* T(int) { return "text"; }
bool backendIsFilaMan() { return true; }
const char* backendBaseUrl() { return "http://fila"; }
const char* filamanApiKey() { return "key"; }
bool wifiManagerIsConnected() { return wifi; }
void loadingOverlayShow(const char*) { assert(loading_depth++==0); ++loading_shown; }
void loadingOverlayHide() { assert(loading_depth--==1); ++loading_hidden; }
void loadingOverlayProgress(size_t) { assert(loading_depth==1); }
int filamanGetSpoolPageJson(const char*,const char*,int,int,JsonDocument& doc,int* total) {
  assert(loading_depth==1 && progress_active); ++pages; *total=1;
  deserializeJson(doc,R"([{"id":123,"filament":{"material":"PLA","name":"Blue"}}])"); return response;
}
bool querySpoolmanById(int id) { assert(adoption_resets==lookups+1); assert(loading_depth==1 && progress_active); ++lookups; sm_id=id; sm_found=lookup_ok; return lookup_ok; }
void clearTagDisplay() { sm_found=false; }
void cancelPendingNfcClear() {}
void cancelRemoteTaglessAdoption() { ++adoption_resets; }
void hideAllOverlays() { hideManualSpoolOverlays(); }
void showMainScreen() {}
void releaseScreen(lv_obj_t** p) { *p=nullptr; }
void showMoreInfoScreen() { assert(loading_depth==0); ++opened; }
void select() {
  for (auto it=objects.rbegin();it!=objects.rend();++it) {
    auto* o=*it; if(o->width==442 && o->cb) { lv_event_t e; e.user_data=o->user_data; o->cb(&e); return; }
  }
  assert(false);
}
void balanced(int before) { assert(loading_shown==before+1 && loading_hidden==loading_shown && loading_depth==0 && !progress_active); }
int main() {
  assert(spoolResolvedForActions(true,123));
  assert(!spoolResolvedForActions(false,123));
  assert(!spoolResolvedForActions(true,0));
  requestManualSpoolScreen(); handleManualSpoolDeferredActions(); balanced(0);
  int before=loading_shown;
  select(); handleManualSpoolDeferredActions(); balanced(before); assert(opened==1 && lookups==1 && adoption_resets==1);
  requestManualSpoolScreen(); handleManualSpoolDeferredActions();
  before=loading_shown; lookup_ok=false;
  select(); handleManualSpoolDeferredActions(); balanced(before); assert(opened==1 && lookups==2 && adoption_resets==2);
  before=loading_shown; tag_present=true;
  select(); handleManualSpoolDeferredActions(); assert(loading_shown==before && lookups==2);
  tag_present=false; wifi=false;
  select(); handleManualSpoolDeferredActions(); assert(loading_shown==before && lookups==2);
  requestManualSpoolScreen(); handleManualSpoolDeferredActions(); assert(loading_shown==before);
  wifi=true; response=500;
  requestManualSpoolScreen(); handleManualSpoolDeferredActions(); balanced(before);
}
"""
    result = subprocess.run(['g++','-std=c++11',f'-I{tmp}',f'-I{root / "src"}',f'-I{root / ".pio/libdeps/wt32-sc01-plus/ArduinoJson/src"}','-x','c++','-',str(root/'src/ui/manual_spool_screen.cpp'),'-o',str(tmp/'check')],input=source,text=True,capture_output=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(tmp/'check')],capture_output=True,text=True)
    assert result.returncode == 0, result.stderr
