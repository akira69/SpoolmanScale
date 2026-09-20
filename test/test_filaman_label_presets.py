#!/usr/bin/env python3
"""Behavioral checks for FilaMan preset selection and parsing."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
json_include = root / ".pio/libdeps/wt32-sc01-plus/ArduinoJson/src"
parser_source = r'''
#include <assert.h>
#include "services/filaman_label_preset_parse.h"

int main() {
  FilaManLabelPreset presets[2]{};
  size_t count = 99;
  bool known = false;
  assert(filamanParseLabelPresets(
      "[{\"id\":7,\"name\":\"Saved\",\"selected\":true}]",
      presets, 2, &count, &known) == 0);
  assert(count == 1 && known && presets[0].selected);

  known = true;
  assert(filamanParseLabelPresets(
      "[{\"id\":7,\"name\":\"Old server\"}]",
      presets, 2, &count, &known) == 0);
  assert(count == 1 && !known && !presets[0].selected);

  assert(filamanParseLabelPresets(
      "[{\"id\":7,\"name\":\"Default\",\"selected\":false}]",
      presets, 2, &count, &known) == 0);
  assert(count == 1 && known && !presets[0].selected);

  known = true;
  assert(filamanParseLabelPresets("[]", presets, 2, &count, &known) == 0);
  assert(count == 0 && !known);

  assert(filamanParseLabelPresets(
      "[{\"id\":7,\"name\":\"One\",\"selected\":false},{\"id\":8,\"name\":\"Two\"}]",
      presets, 2, &count, &known) < 0);
  assert(count == 0 && !known);
  assert(filamanParseLabelPresets(
      "[{\"id\":7,\"name\":\"Bad\",\"selected\":\"false\"}]",
      presets, 2, &count, &known) < 0);
  assert(count == 0 && !known);
  assert(filamanParseLabelPresets(
      "[{\"id\":7,\"name\":\"Saved\",\"selected\":true},{\"id\":-1,\"name\":\"Bad\",\"selected\":false}]",
      presets, 2, &count, &known) < 0);
  assert(count == 0);
}
'''
result = subprocess.run(
    ["g++", "-std=c++11", f"-I{root / 'src'}", f"-I{json_include}", "-x", "c++", "-", "-o", "/tmp/test_filaman_label_presets"],
    input=parser_source, text=True, capture_output=True,
)
assert result.returncode == 0, result.stderr
result = subprocess.run(["/tmp/test_filaman_label_presets"], capture_output=True, text=True)
assert result.returncode == 0, result.stderr

with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    (temp / "lvgl.h").write_text('''
typedef struct { void* user_data; } lv_obj_t;
typedef struct { void* user_data; lv_obj_t* target; } lv_event_t;
static inline void* lv_event_get_user_data(lv_event_t* e) { return e->user_data; }
static inline lv_obj_t* lv_event_get_target(lv_event_t* e) { return e->target; }
static inline void* lv_obj_get_user_data(lv_obj_t* obj) { return obj->user_data; }
''')
    (temp / "services").mkdir()
    (temp / "services/filaman_api.h").write_text('''
#include <stddef.h>
struct FilaManLabelPreset { int id; char name[64]; bool selected; };
''')
    callback_source = r'''
#include <assert.h>
#include "services/filaman_api.h"
#include "ui/label_preset_selection.h"
void requestLabelPresetSelection(int) {}
int main() {
  FilaManLabelPreset presets[2]{{7,"Local",false},{42,"Server",true}};
  assert(filamanResolvedPresetId(presets, 2, true, 7) == 42);
  presets[1].selected = false;
  assert(filamanResolvedPresetId(presets, 2, true, 99) == 0);
  assert(filamanResolvedPresetId(presets, 2, false, 7) == 7);
  assert(filamanResolvedPresetId(presets, 2, false, 99) == 0);
}
'''
    result = subprocess.run(
        ["g++", "-std=c++11", f"-I{temp}", f"-I{root / 'src'}", f"-I{json_include}", "-x", "c++", "-", str(root / "src/ui/label_preset_selection.cpp"), "-o", "/tmp/test_filaman_label_callback"],
        input=callback_source, text=True, capture_output=True,
    )
    assert result.returncode == 0, result.stderr
    result = subprocess.run(["/tmp/test_filaman_label_callback"], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
