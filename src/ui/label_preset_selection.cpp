#include "label_preset_selection.h"

#include <stdint.h>

#include "services/prefs_store.h"
#include "ui/label_print_screen.h"

void labelPresetRowCb(lv_event_t* e) {
  prefsPutInt("label_preset", (int)(intptr_t)lv_event_get_user_data(e));
  requestLabelPresetRefresh();
}
