"""Small LVGL fake shared by the deferred label and manual spool UI checks."""
LVGL_SHIM = r'''
#include <vector>
struct lv_event_t { void* user_data=nullptr; };
using lv_event_cb_t = void (*)(lv_event_t*);
struct lv_obj_t { int x=0, y=0, width=0, bg=0; bool disabled=false; lv_event_cb_t cb=nullptr; void* user_data=nullptr; };
extern std::vector<lv_obj_t*> objects;
inline lv_obj_t* lv_obj_create(lv_obj_t*) { auto* o=new lv_obj_t; objects.push_back(o); return o; }
inline lv_obj_t* lv_btn_create(lv_obj_t* p) { return lv_obj_create(p); }
inline lv_obj_t* lv_label_create(lv_obj_t* p) { return lv_obj_create(p); }
inline lv_obj_t* lv_canvas_create(lv_obj_t* p) { return lv_obj_create(p); }
inline lv_obj_t* lv_scr_act() { return nullptr; }
inline void lv_obj_set_size(lv_obj_t* o, int w, int) { o->width=w; }
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
inline void lv_obj_clear_state(lv_obj_t* o, int) { o->disabled=false; }
inline void lv_obj_add_state(lv_obj_t* o, int) { o->disabled=true; }
inline void lv_obj_clean(lv_obj_t*) {}
inline void lv_obj_center(lv_obj_t*) {}
inline void lv_obj_align(lv_obj_t*, int, int, int) {}
inline void lv_obj_add_event_cb(lv_obj_t* o, lv_event_cb_t cb, int, void* data) { o->cb=cb; o->user_data=data; }
inline void lv_label_set_text(lv_obj_t*, const char*) {}
inline void lv_label_set_text_fmt(lv_obj_t*, const char*, ...) {}
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
'''
