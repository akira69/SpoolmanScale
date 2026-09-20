#include "services/label_printer.h"
#include "services/phomemo_m220.h"
#include "services/phomemo_m_series_protocol.h"
#include <array>
#include <cassert>

int main() {
  const std::array<uint8_t, 8> m220_header = {0x1d, 0x76, 0x30, 0x00, 0x48, 0x00, 0x90, 0x01};
  assert(phomemoRasterHeader(576, 400) == m220_header);
  assert(phomemoWriteChunk(23) == 20);
  assert(phomemoWriteChunk(247) == 128);
  assert(m110SpeedCommand(5) ==
         (std::array<uint8_t, 4>{0x1b, 0x4e, 0x0d, 0x05}));
  assert(m110DensityCommand(10) ==
         (std::array<uint8_t, 4>{0x1b, 0x4e, 0x04, 0x0a}));
  assert(m110MediaCommand(0x0a) ==
         (std::array<uint8_t, 3>{0x1f, 0x11, 0x0a}));
  assert(m110FooterStart() ==
         (std::array<uint8_t, 4>{0x1f, 0xf0, 0x05, 0x00}));
  assert(m110FooterEnd() ==
         (std::array<uint8_t, 4>{0x1f, 0xf0, 0x03, 0x00}));
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
  assert(m220DotsForMm(40) == 320);
  assert(m220DotsForMm(30) == 240);
  assert(m220RasterWidthForMedia(40) == 576);
  assert(m220RasterWidthForMedia(75) == 600);
  LabelRaster media{576, 240, 72, rotated_rows, sizeof(rotated_rows), 320, false};
  assert(labelRasterFitsM220Media(media, 40, 30));
  assert(!labelRasterFitsM220Media(media, 30, 40));
}
