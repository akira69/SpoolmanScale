#pragma once

#include <stddef.h>
#include <lvgl.h>

struct FilaManLabelPreset;

int filamanResolvedPresetId(const FilaManLabelPreset* presets, size_t count,
                            bool selection_known, int local_id);
void requestLabelPresetSelection(int preset_id);
void labelPresetRowCb(lv_event_t* e);
