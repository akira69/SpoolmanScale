#!/usr/bin/env python3
"""Behavioral checks for FilaMan preset selection and parsing."""
import subprocess
from pathlib import Path

root = Path(__file__).parents[1]
json_include = root / ".pio/libdeps/wt32-sc01-plus/ArduinoJson/src"
source = r'''
#include <assert.h>
#include "services/filaman_label_preset_parse.h"

int main() {
  assert(filamanLabelPresetId((void*)7) == 7);
  FilaManLabelPreset presets[2];
  size_t count = 99;
  assert(filamanParseLabelPresets("[{\"id\":7,\"name\":\"Saved\"},{\"id\":-1,\"name\":\"Bad\"}]", presets, 2, &count) < 0);
  assert(count == 0);
}
'''
result = subprocess.run(
    ["g++", "-std=c++11", f"-I{root / 'src'}", f"-I{json_include}", "-x", "c++", "-", "-o", "/tmp/test_filaman_label_presets"],
    input=source, text=True, capture_output=True,
)
assert result.returncode == 0, result.stderr
result = subprocess.run(["/tmp/test_filaman_label_presets"], capture_output=True, text=True)
assert result.returncode == 0, result.stderr
