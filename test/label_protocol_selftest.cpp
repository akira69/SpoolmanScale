#include "services/label_raster.h"
#include "services/phomemo_m220_protocol.h"
#include <array>
#include <cassert>

int main() {
  const std::array<uint8_t, 8> expected = {0x1d, 0x76, 0x30, 0x00, 0x48, 0x00, 0x90, 0x01};
  assert(m220RasterHeader(576, 400) == expected);
  assert(m220WriteChunk(23) == 20);
  assert(m220WriteChunk(247) == 128);
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
  uint8_t rotated_rows[96] = {};
  LabelRaster rotated{384, 2, 48, rotated_rows, sizeof(rotated_rows), 3, true};
  // A 2x3 portrait preview maps back from the right-aligned, rotated print rows.
  rotated_rows[48 + 47] = 0x04;
  assert(labelRasterPreviewBlack(rotated, 0, 0));
  assert(!labelRasterPreviewBlack(rotated, 1, 0));
}
