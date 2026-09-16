#include "services/label_raster.h"
#include <cassert>

int main() {
  assert(labelRasterShapeValid(480, 320, 60, 19200));
  assert(!labelRasterShapeValid(480, 320, 60, 19199));
  assert(!labelRasterShapeValid(480, 320, 61, 19520));
  assert(!labelRasterShapeValid(0, 320, 0, 0));
  uint8_t row[49] = {};
  row[48] = 0x80;
  LabelRaster image{385, 1, 49, row, sizeof(row)};
  assert(labelRasterPaddingValid(image));
  row[48] = 0x81;
  assert(!labelRasterPaddingValid(image));
}
