#pragma once

#include <stddef.h>

inline bool labelPresetsUseGroups(size_t count) { return count > 10; }

inline int labelPresetGroup(const char* name) {
  const unsigned char initial = name && *name ? (unsigned char)*name : 0;
  const char letter = initial >= 'a' && initial <= 'z' ? initial - 'a' + 'A' : initial;
  if (letter >= 'A' && letter <= 'F') return 0;
  if (letter >= 'G' && letter <= 'L') return 1;
  if (letter >= 'M' && letter <= 'R') return 2;
  if (letter >= 'S' && letter <= 'Z') return 3;
  return 4;
}
