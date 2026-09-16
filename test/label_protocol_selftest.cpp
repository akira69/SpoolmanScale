#include "services/label_raster.h"
#include "services/phomemo_m220_protocol.h"
#include <array>
#include <cassert>

int main() {
  const std::array<uint8_t, 8> expected = {0x1d, 0x76, 0x30, 0x00, 0x48, 0x00, 0x90, 0x01};
  assert(m220RasterHeader(576, 400) == expected);
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
