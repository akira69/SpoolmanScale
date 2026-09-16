#!/usr/bin/env python3
"""Small contract check for FilaMan's bounded label preset client."""
from pathlib import Path

root = Path(__file__).parents[1]
header = (root / "src/services/filaman_api.h").read_text()
source = (root / "src/services/filaman_api.cpp").read_text()

assert "struct FilaManLabelPreset" in header
assert "int filamanListLabelPresets(" in header
assert '"/api/v1/labels/presets"' in source
assert "addApiKey(http, api_key)" in source
assert "FILAMAN_LABEL_PRESET_MAX" in source
assert "*count = 0" in source
