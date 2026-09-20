#include "label_preset_selection.h"

#include <stdint.h>

#include "services/filaman_api.h"

int filamanResolvedPresetId(const FilaManLabelPreset* presets, size_t count,
                            bool selection_known, int local_id) {
  if (selection_known) {
    for (size_t i = 0; i < count; ++i)
      if (presets[i].selected) return presets[i].id;
    return 0;
  }
  if (local_id == 0) return 0;
  for (size_t i = 0; i < count; ++i)
    if (presets[i].id == local_id) return local_id;
  return 0;
}

void labelPresetRowCb(lv_event_t* e) {
  requestLabelPresetSelection((int)(intptr_t)lv_event_get_user_data(e));
}
