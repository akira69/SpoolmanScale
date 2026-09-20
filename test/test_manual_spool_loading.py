#!/usr/bin/env python3
"""Exercise manual spool search and loading against the production UI."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)

    def header(name, body):
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#pragma once\n" + body)

    header("lvgl.h", r'''
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <vector>
struct lv_event_t { void* user_data = nullptr; };
using lv_event_cb_t = void (*)(lv_event_t*);
struct Handler { int event; lv_event_cb_t cb; void* data; };
struct lv_obj_t {
  int x=0, y=0, width=0, height=0, bg=0, kind=0, max_length=0;
  std::string text;
  bool deleted=false, hidden=false, disabled=false;
  lv_obj_t* parent=nullptr;
  std::vector<Handler> handlers;
};
extern std::vector<lv_obj_t*> objects;
inline lv_obj_t* lv_obj_create(lv_obj_t* p) { auto* o=new lv_obj_t; o->parent=p; objects.push_back(o); return o; }
inline lv_obj_t* lv_btn_create(lv_obj_t* p) { auto* o=lv_obj_create(p); o->kind=1; return o; }
inline lv_obj_t* lv_label_create(lv_obj_t* p) { auto* o=lv_obj_create(p); o->kind=2; return o; }
inline lv_obj_t* lv_textarea_create(lv_obj_t* p) { auto* o=lv_obj_create(p); o->kind=3; return o; }
inline lv_obj_t* lv_keyboard_create(lv_obj_t* p) { auto* o=lv_obj_create(p); o->kind=4; return o; }
inline lv_obj_t* lv_scr_act() { return nullptr; }
inline void lv_obj_set_size(lv_obj_t* o,int w,int h) { o->width=w; o->height=h; }
inline void lv_obj_set_width(lv_obj_t* o,int w) { o->width=w; }
inline void lv_obj_set_pos(lv_obj_t* o,int x,int y) { o->x=x; o->y=y; }
inline void lv_obj_set_style_bg_color(lv_obj_t* o,int c,int) { o->bg=c; }
inline void lv_obj_set_style_radius(lv_obj_t*,int,int) {}
inline void lv_obj_set_style_border_width(lv_obj_t*,int,int) {}
inline void lv_obj_set_style_border_color(lv_obj_t*,int,int) {}
inline void lv_obj_set_style_pad_all(lv_obj_t*,int,int) {}
inline void lv_obj_set_style_pad_row(lv_obj_t*,int,int) {}
inline void lv_obj_set_style_text_color(lv_obj_t*,int,int) {}
inline void lv_obj_set_style_text_font(lv_obj_t*,const int*,int) {}
inline void lv_obj_set_style_text_align(lv_obj_t*,int,int) {}
inline void lv_obj_set_flex_flow(lv_obj_t*,int) {}
inline void lv_obj_set_scroll_dir(lv_obj_t*,int) {}
inline void lv_obj_clear_flag(lv_obj_t* o,int f) { if(f==2) o->hidden=false; }
inline void lv_obj_add_flag(lv_obj_t* o,int f) { if(f==2) o->hidden=true; }
inline void lv_obj_clear_state(lv_obj_t* o,int) { o->disabled=false; }
inline void lv_obj_add_state(lv_obj_t* o,int) { o->disabled=true; }
inline void lv_obj_clean(lv_obj_t* p) { for(auto* o:objects) if(o->parent==p) o->deleted=true; }
inline void lv_obj_center(lv_obj_t*) {}
inline void lv_obj_align(lv_obj_t*,int,int,int) {}
inline void lv_obj_add_event_cb(lv_obj_t* o,lv_event_cb_t cb,int event,void* data) { o->handlers.push_back({event,cb,data}); }
inline void lv_label_set_text(lv_obj_t* o,const char* s) { o->text=s ? s : ""; }
inline void lv_label_set_text_fmt(lv_obj_t* o,const char* fmt,...) { char b[160]; va_list ap; va_start(ap,fmt); vsnprintf(b,sizeof(b),fmt,ap); va_end(ap); o->text=b; }
inline void lv_label_set_long_mode(lv_obj_t*,int) {}
inline void* lv_event_get_user_data(lv_event_t* e) { return e->user_data; }
inline void lv_textarea_set_one_line(lv_obj_t*,bool) {}
inline void lv_textarea_set_placeholder_text(lv_obj_t*,const char*) {}
inline void lv_textarea_set_max_length(lv_obj_t* o,int n) { o->max_length=n; }
inline void lv_textarea_set_text(lv_obj_t* o,const char* s) { o->text=s ? s : ""; }
inline const char* lv_textarea_get_text(lv_obj_t* o) { return o->text.c_str(); }
inline void lv_keyboard_set_textarea(lv_obj_t*,lv_obj_t*) {}
inline void lv_obj_del(lv_obj_t* o) { o->deleted=true; }
inline int lv_color_hex(int v) { return v; }
static const int lv_font_montserrat_ext_14=0, lv_font_montserrat_ext_16=0;
#define LV_EVENT_CLICKED 1
#define LV_EVENT_READY 2
#define LV_EVENT_CANCEL 3
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OBJ_FLAG_HIDDEN 2
#define LV_LABEL_LONG_DOT 1
#define LV_ALIGN_TOP_MID 1
#define LV_ALIGN_BOTTOM_MID 2
#define LV_TEXT_ALIGN_CENTER 1
#define LV_FLEX_FLOW_COLUMN 1
#define LV_STATE_DISABLED 1
#define LV_DIR_VER 1
#define LV_SYMBOL_LEFT "<"
#define LV_SYMBOL_RIGHT ">"
#define LV_SYMBOL_LIST "="
#define LV_SYMBOL_SEARCH "?"
''')

    header("lang.h", '''
enum { STR_SPOOLS_TITLE, STR_SPOOLS_SEARCH, STR_SPOOLS_CLEAR,
  STR_SPOOLS_SEARCH_HINT, STR_SPOOLS_SEARCH_EMPTY, STR_LABEL_NO_WIFI,
  STR_SPOOLS_LOADING, STR_SPOOLS_LOAD_FAIL, STR_SPOOLS_LOW_MEM,
  STR_SPOOLS_EMPTY, STR_SPOOLS_PAGE_FMT, STR_SPOOLS_REMOVE_TAG,
  STR_SPOOLS_OPENING, STR_SPOOLS_OPEN_FAIL };
const char* T(int);
''')
    header("Arduino.h", "#include <stddef.h>\n#include <stdint.h>\n#include <string.h>\n")
    header("esp_heap_caps.h", '''
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
inline void* heap_caps_malloc(size_t n,int) { return malloc(n); }
inline void heap_caps_free(void* p) { free(p); }
inline void* heap_caps_realloc(void* p,size_t n,int) { return realloc(p,n); }
''')
    header("app/app_state.h", "extern bool tag_present, sm_found; extern int sm_id;\n")
    header("app/app_loop.h", "void cancelPendingNfcClear(); void cancelRemoteTaglessAdoption();\n")
    header("hardware/sd_logger.h", "")
    header("services/backend.h", "bool backendIsFilaMan(); const char* backendBaseUrl(); const char* filamanApiKey();\n")
    header("services/list_limits.h", "extern int spool_list_limit;\n")
    header("services/filaman_api.h", '''
#include <ArduinoJson.h>
int filamanGetSpoolPageJson(const char*,const char*,int,int,JsonDocument&,int*,
                            const char* = nullptr,uint32_t = 8000);
''')
    header("services/http_progress.h", '''
#include <stddef.h>
extern bool progress_active;
struct HttpStall { explicit HttpStall(void (*)(size_t)) { progress_active=true; }
  ~HttpStall() { progress_active=false; } };
''')
    header("services/wifi_manager.h", "bool wifiManagerIsConnected();\n")
    header("ui/loading_overlay.h", "void loadingOverlayShow(const char*); void loadingOverlayHide(); void loadingOverlayProgress(size_t);\n")
    header("ui/header_status.h", "void updateHeaderStatus();\n")
    header("ui/more_info_screen.h", "void showMoreInfoScreen();\n")
    header("ui/navigation.h", "void hideAllOverlays(); void showMainScreen();\n")
    header("ui/spoolman_lookup.h", "bool querySpoolmanById(int);\n")
    header("ui/tag_display.h", "void clearTagDisplay();\n")
    header("ui/ui_common.h", '''
#include <lvgl.h>
void releaseScreen(lv_obj_t**);
inline bool lvPoolHasRoomForRow() { return true; }
inline void styleOutlineButton(lv_obj_t*) {}
inline void styleListPanel(lv_obj_t*) {}
inline void styleListRow(lv_obj_t*,bool=false) {}
inline lv_obj_t* buildOverlayScreen() { auto* o=lv_obj_create(nullptr); lv_obj_set_size(o,480,320); return o; }
inline void buildSubHeader(lv_obj_t* p,const char*,lv_event_cb_t cb,const char* = nullptr) {
  auto* b=lv_btn_create(p); lv_obj_set_size(b,44,44); lv_obj_set_pos(b,4,2);
  lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr);
}
''')

    source = r'''
#include <assert.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include "lang.h"
#include "ui/manual_spool_screen.h"
#include "ui/main_screen_helpers.h"
std::vector<lv_obj_t*> objects;
std::vector<int> requested_pages, requested_page_sizes;
std::vector<std::string> requested_searches;
bool wifi=true, tag_present=false, sm_found=false, lookup_ok=true, progress_active=false;
int sm_id=0, spool_list_limit=20, response=200, response_items=10, response_total=25;
int lookups=0, opened=0, adoption_resets=0, header_updates=0, released=0;
int loading_depth=0, loading_shown=0, loading_hidden=0;
const char* T(int id) {
  if (id==STR_SPOOLS_SEARCH_EMPTY) return "search-empty";
  if (id==STR_SPOOLS_EMPTY) return "inventory-empty";
  if (id==STR_SPOOLS_LOAD_FAIL) return "load-fail-%d";
  return "text";
}
bool backendIsFilaMan() { return true; }
const char* backendBaseUrl() { return "http://fila"; }
const char* filamanApiKey() { return "key"; }
bool wifiManagerIsConnected() { return wifi; }
void loadingOverlayShow(const char*) { assert(loading_depth++==0); ++loading_shown; }
void loadingOverlayHide() { assert(loading_depth--==1); ++loading_hidden; }
void loadingOverlayProgress(size_t) { assert(loading_depth==1); }
int filamanGetSpoolPageJson(const char*,const char*,int page,int page_size,
                            JsonDocument& doc,int* total,const char* search,uint32_t) {
  assert(loading_depth==1 && progress_active);
  requested_pages.push_back(page);
  requested_page_sizes.push_back(page_size);
  requested_searches.push_back(search ? search : "");
  *total=response_total;
  JsonArray dst=doc.to<JsonArray>();
  for(int i=0;i<response_items;++i) {
    JsonObject spool=dst.add<JsonObject>();
    spool["id"]=123+i;
    JsonObject filament=spool["filament"].to<JsonObject>();
    filament["material"]="PLA";
    filament["name"]="Blue";
  }
  return response;
}
bool querySpoolmanById(int id) { assert(adoption_resets==lookups+1); assert(loading_depth==1 && progress_active); ++lookups; sm_id=id; sm_found=lookup_ok; return lookup_ok; }
void clearTagDisplay() { sm_found=false; }
void cancelPendingNfcClear() {}
void cancelRemoteTaglessAdoption() { ++adoption_resets; }
void updateHeaderStatus() { assert(sm_found && sm_id>0); ++header_updates; }
void markDeleted(lv_obj_t* p) { if(!p || p->deleted) return; p->deleted=true; for(auto* o:objects) if(o->parent==p) markDeleted(o); }
void releaseScreen(lv_obj_t** p) { if(*p) { markDeleted(*p); ++released; } *p=nullptr; }
void hideAllOverlays() { hideManualSpoolOverlays(); }
void showMainScreen() {}
void showMoreInfoScreen() { assert(loading_depth==0); ++opened; }

void fire(lv_obj_t* o,int event) {
  assert(o && !o->deleted && !o->hidden);
  for(auto h:o->handlers) if(h.event==event) { lv_event_t e; e.user_data=h.data; h.cb(&e); }
}
lv_obj_t* newest(int kind,int x,int y,int w,int h) {
  for(auto it=objects.rbegin();it!=objects.rend();++it) {
    auto* o=*it;
    if(!o->deleted && !o->hidden && o->kind==kind && o->x==x && o->y==y &&
       o->width==w && o->height==h) return o;
  }
  assert(false); return nullptr;
}
lv_obj_t* newestKind(int kind) {
  for(auto it=objects.rbegin();it!=objects.rend();++it)
    if(!(*it)->deleted && !(*it)->hidden && (*it)->kind==kind) return *it;
  assert(false); return nullptr;
}
int liveRows() {
  int n=0; for(auto* o:objects) if(!o->deleted && o->kind==1 && o->width==442 && o->height==42) ++n;
  return n;
}
bool liveText(const char* text) {
  for(auto* o:objects) if(!o->deleted && o->text==text) return true;
  return false;
}
void tap(int x,int y,int w,int h) { fire(newest(1,x,y,w,h),LV_EVENT_CLICKED); }
void openSearch() { tap(80,52,200,44); handleManualSpoolDeferredActions(); }
void enterSearch(const char* text) { lv_textarea_set_text(newestKind(3),text); }
void submitKeyboard() { fire(newestKind(4),LV_EVENT_READY); }
void cancelKeyboard() { fire(newestKind(4),LV_EVENT_CANCEL); }
void balanced(int before) { assert(loading_shown==before+1 && loading_hidden==loading_shown && loading_depth==0 && !progress_active); }

int main() {
  assert(spoolResolvedForActions(true,123));
  assert(!spoolResolvedForActions(false,123));
  assert(!spoolResolvedForActions(true,0));

  requestManualSpoolScreen(); handleManualSpoolDeferredActions();
  assert(requested_pages==std::vector<int>{1});
  assert(requested_page_sizes==std::vector<int>{10});
  assert(requested_searches==std::vector<std::string>{""});
  assert(liveRows()==10);
  balanced(0);

  openSearch();
  lv_obj_t* input=newestKind(3);
  assert(input->x==10 && input->y==56 && input->width==460 && input->height==44);
  assert(input->max_length==200);
  assert(newest(1,260,106,210,44));  // visible submit action
  assert(newestKind(4)->width==480 && newestKind(4)->height==160);
  enterSearch("  #123 & blue  ");
  submitKeyboard();
  assert(requested_pages.size()==1);  // event callbacks never perform HTTP
  handleManualSpoolDeferredActions();
  assert(requested_pages.back()==1 && requested_page_sizes.back()==10);
  assert(requested_searches.back()=="#123 & blue");

  tap(420,52,50,44);  // next
  assert(requested_pages.size()==2);
  handleManualSpoolDeferredActions();
  assert(requested_pages.back()==2 && requested_searches.back()=="#123 & blue");

  openSearch();
  enterSearch("discarded");
  cancelKeyboard();
  handleManualSpoolDeferredActions();
  assert(requested_pages.size()==3);
  tap(420,52,50,44); handleManualSpoolDeferredActions();
  assert(requested_pages.back()==3 && requested_searches.back()=="#123 & blue");

  tap(290,52,120,44);  // clear
  handleManualSpoolDeferredActions();
  assert(requested_pages.back()==1 && requested_searches.back()=="");

  openSearch(); enterSearch(" \t\r\n "); submitKeyboard();
  handleManualSpoolDeferredActions();
  assert(requested_pages.back()==1 && requested_searches.back()=="");

  response_items=0; response_total=0;
  openSearch(); enterSearch("missing"); submitKeyboard();
  handleManualSpoolDeferredActions();
  assert(requested_searches.back()=="missing" && liveText("search-empty"));

  int before=loading_shown;
  response=500;
  openSearch(); enterSearch("failure"); submitKeyboard();
  handleManualSpoolDeferredActions();
  balanced(before);
  assert(liveText("load-fail-500"));

  openSearch();
  int releases_before=released;
  hideManualSpoolOverlays();
  assert(released==releases_before+2);  // list and keyboard screens
  response=200; response_items=10; response_total=25;
  requestManualSpoolScreen(); handleManualSpoolDeferredActions();
  assert(requested_pages.back()==1 && requested_searches.back()=="");

  before=loading_shown;
  fire(newest(1,0,0,442,42),LV_EVENT_CLICKED);
  handleManualSpoolDeferredActions();
  balanced(before);
  assert(opened==1 && lookups==1 && adoption_resets==1 && header_updates==1);

  requestManualSpoolScreen(); handleManualSpoolDeferredActions();
  before=loading_shown; lookup_ok=false;
  fire(newest(1,0,0,442,42),LV_EVENT_CLICKED);
  handleManualSpoolDeferredActions(); balanced(before);
  assert(opened==1 && lookups==2 && adoption_resets==2 && header_updates==1);
  before=loading_shown; tag_present=true;
  fire(newest(1,0,0,442,42),LV_EVENT_CLICKED);
  handleManualSpoolDeferredActions(); assert(loading_shown==before && lookups==2);
  tag_present=false; wifi=false;
  fire(newest(1,0,0,442,42),LV_EVENT_CLICKED);
  handleManualSpoolDeferredActions(); assert(loading_shown==before && lookups==2);
  requestManualSpoolScreen(); handleManualSpoolDeferredActions();
  assert(loading_shown==before);
  wifi=true; response=500;
  requestManualSpoolScreen(); handleManualSpoolDeferredActions(); balanced(before);
}
'''
    result = subprocess.run(
        ["g++", "-std=c++11", f"-I{tmp}", f"-I{root / 'src'}",
         f"-I{root / '.pio/libdeps/wt32-sc01-plus/ArduinoJson/src'}",
         "-x", "c++", "-", str(root / "src/ui/manual_spool_screen.cpp"),
         "-o", str(tmp / "check")],
        input=source, text=True, capture_output=True,
    )
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(tmp / "check")], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
