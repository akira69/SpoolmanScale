#include "services/phomemo_m_series.h"
#include "services/phomemo_m_series_protocol.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>

#include "lang.h"
#include "services/breadcrumb.h"

namespace {
bool s_client_unresolved = false;

class MSeriesScanCollector : public NimBLEScanCallbacks {
 public:
  MSeriesScanCollector(LabelPrinterDevice* devices, size_t max_devices,
                       const LabelPrinterConfig& config)
      : out(devices), capacity(max_devices), selected(config) {}

  void onDiscovered(const NimBLEAdvertisedDevice* device) override { consider(device); }
  void onResult(const NimBLEAdvertisedDevice* device) override { consider(device); }

  size_t count = 0;
  bool selectedFound() const { return selected_found; }

 private:
  void consider(const NimBLEAdvertisedDevice* device) {
    LabelPrinterDevice candidate{};
    snprintf(candidate.name, sizeof(candidate.name), "%s", device->getName().c_str());
    snprintf(candidate.address, sizeof(candidate.address), "%s", device->getAddress().toString().c_str());
    if (selected.address[0] && !strcmp(candidate.address, selected.address)) selected_found = true;
    labelPrinterConsiderDevice(out, &count, capacity, candidate, selected);
  }

  LabelPrinterDevice* out;
  size_t capacity;
  const LabelPrinterConfig& selected;
  bool selected_found = false;
};
}

size_t phomemoMSeriesScan(LabelPrinterDevice* out, size_t capacity,
                        const LabelPrinterConfig& selected, LabelPrinterProgressFn progress) {
  if (s_client_unresolved) return 0;
  if (!NimBLEDevice::init("")) return 0;
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setMaxResults(0);
  MSeriesScanCollector collector(out, capacity, selected);
  // Include scan responses, which often carry the printer name.
  scan->setScanCallbacks(&collector, true);
  for (unsigned interval = 0; interval < 8; ++interval) {
    scan->getResults(1000);
    if (progress) progress();
    if (labelPrinterConfigured(selected) && collector.selectedFound()) break;
  }
  scan->setScanCallbacks(nullptr);
  NimBLEDevice::deinit(true);
  return collector.count;
}

bool phomemoMSeriesPrint(LabelPrinterModel model, const char* address, const LabelRaster& image,
                        char* error, size_t error_size, LabelPrinterProgressFn progress) {
  const LabelPrinterProfile& profile = labelPrinterProfile(model);
  auto fail = [&](const char* message) { if (error_size) snprintf(error, error_size, "%s: %s", profile.name ? profile.name : "Printer", message); return false; };
  if (error_size) error[0] = 0;
  if (profile.model == LabelPrinterModel::NONE) return fail("Select a printer model first.");
  if (!address || !*address) return fail("Select a printer first.");
  if (!labelRasterPaddingValid(image)) return fail("Invalid label image.");
  if (image.width > profile.max_raster_width) return fail("width exceeds print head.");
  if (s_client_unresolved) return fail("BLE cleanup pending. Restart scale before retrying.");
  Serial.printf("%s before client: heap=%u largest=%u psram=%u\n", profile.name,
                unsigned(ESP.getFreeHeap()),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                unsigned(ESP.getFreePsram()));
  crumbSet(LABEL_PRINTER_BLE_START_CRUMB);
  if (!NimBLEDevice::init("")) return fail("BLE unavailable.");
  crumbSet("label printer BLE ready");
  NimBLEDevice::setMTU(PHOMEMO_PREFERRED_MTU);
  NimBLEClient* client = NimBLEDevice::createClient();
  if (!client) {
    NimBLEDevice::deinit(true);
    return fail("BLE client unavailable.");
  }
  bool ok = false;
  do {
    if (!client->connect(NimBLEAddress(address, BLE_ADDR_PUBLIC))) {
      fail(T(STR_LABEL_PRINTER_CONNECT_RETRY));
      break;
    }
    NimBLERemoteService* service = client->getService(NimBLEUUID((uint16_t)0xff00));
    NimBLERemoteCharacteristic* write = service ? service->getCharacteristic(NimBLEUUID((uint16_t)0xff02)) : nullptr;
    if (!write || (!write->canWrite() && !write->canWriteNoResponse())) {
      fail("write characteristic missing."); break;
    }
    const size_t chunk = phomemoWriteChunk(client->getMTU());
    const bool response = !write->canWriteNoResponse();
    Serial.printf("%s BLE connected: MTU=%u chunk=%u response=%u raster=%ux%u (%u bytes) heap=%u largest=%u psram=%u\n", profile.name,
                  client->getMTU(), unsigned(chunk), unsigned(response),
                  unsigned(image.width), unsigned(image.height), unsigned(image.length),
                  unsigned(ESP.getFreeHeap()),
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  unsigned(ESP.getFreePsram()));
    auto send = [&](const char* stage, const uint8_t* data, size_t length) {
      for (size_t pos = 0; pos < length; pos += chunk) {
        if (!client->isConnected()) {
          Serial.printf("%s %s disconnected at %u/%u\n", profile.name, stage, unsigned(pos), unsigned(length));
          return false;
        }
        if (!write->writeValue(data + pos, min(chunk, length - pos), response)) {
          Serial.printf("%s %s write failed at %u/%u\n", profile.name, stage, unsigned(pos), unsigned(length));
          return false;
        }
        if (!client->isConnected()) {
          Serial.printf("%s %s disconnected after %u/%u\n", profile.name, stage, unsigned(pos), unsigned(length));
          return false;
        }
        delay(20);
        if (progress) progress();
        if (!strcmp(stage, "raster") && pos && pos % 4096 < chunk)
          Serial.printf("%s raster %u/%u heap=%u\n", profile.name, unsigned(pos), unsigned(length), unsigned(ESP.getFreeHeap()));
      }
      Serial.printf("%s %s sent %u bytes, heap=%u largest=%u\n", profile.name, stage, unsigned(length),
                    unsigned(ESP.getFreeHeap()),
                    unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
      return true;
    };
    const auto header = phomemoRasterHeader(image.width, image.height);
    if (model == LabelPrinterModel::M220) {
      const uint8_t init[] = {0x1b, 0x40};
      const uint8_t density[] = {0x1b, 0x37, 0x07, 0x64, 0x64};
      const uint8_t feed[] = {0x1b, 0x4a, 0x20};
      if (!send("init", init, sizeof(init)) || !send("density", density, sizeof(density)) ||
          !send("header", header.data(), header.size()) || !send("raster", image.pixels, image.length) ||
          !send("feed", feed, sizeof(feed))) { fail("write failed or disconnected."); break; }
    } else {
      const auto speed = m110SpeedCommand(5);
      const auto density = m110DensityCommand(10);
      const auto media = m110MediaCommand(0x0a);
      const auto footer_start = m110FooterStart();
      const auto footer_end = m110FooterEnd();
      if (!send("speed", speed.data(), speed.size()) || !send("density", density.data(), density.size()) ||
          !send("media", media.data(), media.size()) || !send("header", header.data(), header.size()) ||
          !send("raster", image.pixels, image.length) ||
          !send("footer start", footer_start.data(), footer_start.size()) ||
          !send("footer end", footer_end.data(), footer_end.size())) {
        fail("write failed or disconnected."); break;
      }
    }
    ok = true;
  } while (false);
  if (client->isConnected()) client->disconnect();
  const uint32_t until = millis() + 3000;
  while (client->isConnected() && (int32_t)(until - millis()) > 0) delay(10);
  if (client->isConnected()) {
    // ponytail: retain one unresolved client; a late BLE callback could use it.
    s_client_unresolved = true;
    return fail("disconnect timed out. Restart scale before retrying.");
  }
  // Stopping the host first lets deinit delete the client after callbacks end.
  NimBLEDevice::deinit(true);
  return ok;
}
