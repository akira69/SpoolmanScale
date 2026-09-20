#include <assert.h>
#include <string.h>

#include <string>

#include "services/filaman_label_preset_parse.h"
#include "services/phomemo_m220.h"
#include "ui/label_preset_group.h"

int main() {
  FilaManLabelPreset presets[100]{};
  size_t count = 0;
  std::string body = "[";
  for (int i = 1; i <= 100; ++i) {
    if (i > 1) body += ',';
    body += "{\"id\":" + std::to_string(i) + ",\"name\":\"" + std::string(120, 'A') + "\"}";
  }
  body += ']';
  assert(filamanParseLabelPresets(body.c_str(), presets, 100, &count) == 0);
  assert(count == 100);
  assert(presets[0].id == 1 && presets[99].id == 100);
  assert(strlen(presets[0].name) == sizeof(presets[0].name) - 1);
  std::string unicode = "[{\"id\":1,\"name\":\"" + std::string(40, 'B') +
                        u8"éééééééééééééééééééé" + "\"}]";
  assert(filamanParseLabelPresets(unicode.c_str(), presets, 100, &count) == 0);
  assert(count == 1);
  assert(strlen(presets[0].name) <= 63);
  assert((unsigned char)presets[0].name[strlen(presets[0].name) - 1] == 0xA9);
  assert(labelPresetGroup("Alpha") == 0);
  assert(labelPresetGroup("gizmo") == 1);
  assert(labelPresetGroup("Maker") == 2);
  assert(labelPresetGroup("Zebra") == 3);
  assert(labelPresetGroup("40 mm") == 4);
  assert(!labelPresetsUseGroups(0));
  assert(!labelPresetsUseGroups(10));
  assert(labelPresetsUseGroups(11));
  LabelRaster correct{576, 240, 72, nullptr, 0, 320, false};
  assert(labelRasterFitsM220Media(correct, 40, 30));
  correct.content_width = 480;
  assert(!labelRasterFitsM220Media(correct, 40, 30));
}
