#include "services/phomemo_m220.h"
#include "services/phomemo_m220_protocol.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_gattc_api.h>
#include <cstdio>
#include <cstring>

size_t phomemoM220Scan(M220Device* out, size_t capacity) {
  BLEDevice::init("");
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  BLEScanResults results = scan->start(4, false);
  size_t count = 0;
  for (int i = 0; i < results.getCount() && count < capacity; ++i) {
    BLEAdvertisedDevice device = results.getDevice(i);
    std::string name = device.getName();
    if (name.find("M220") == std::string::npos) continue;
    snprintf(out[count].name, sizeof(out[count].name), "%s", name.c_str());
    snprintf(out[count].address, sizeof(out[count].address), "%s", device.getAddress().toString().c_str());
    ++count;
  }
  scan->clearResults();
  return count;
}

bool phomemoM220Print(const char* address, const LabelRaster& image,
                     char* error, size_t error_size) {
  auto fail = [&](const char* message) { snprintf(error, error_size, "%s", message); return false; };
  if (error_size) error[0] = 0;
  if (!address || !*address) return fail("Select an M220 printer first.");
  if (!labelRasterPaddingValid(image)) return fail("Invalid label image.");
  BLEDevice::init("");
  BLEClient* client = BLEDevice::createClient();
  bool ok = false;
  do {
    if (!client->connect(BLEAddress(address))) { fail("Could not connect to M220."); break; }
    BLERemoteService* service = client->getService(BLEUUID((uint16_t)0xff00));
    BLERemoteCharacteristic* write = service ? service->getCharacteristic(BLEUUID((uint16_t)0xff02)) : nullptr;
    if (!write || (!write->canWrite() && !write->canWriteNoResponse())) {
      fail("M220 write characteristic missing."); break;
    }
    const size_t chunk = m220WriteChunk(client->getMTU());
    const bool response = write->canWrite();
    auto send = [&](const uint8_t* data, size_t length) {
      for (size_t pos = 0; pos < length; pos += chunk) {
        if (!client->isConnected()) return false;
        esp_err_t result = esp_ble_gattc_write_char(
            client->getGattcIf(), client->getConnId(), write->getHandle(),
            min(chunk, length - pos), const_cast<uint8_t*>(data + pos),
            response ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP,
            ESP_GATT_AUTH_REQ_NONE);
        if (result != ESP_OK) return false;
        if (!client->isConnected()) return false;
        delay(20);
      }
      return true;
    };
    const uint8_t init[] = {0x1b, 0x40};
    const uint8_t density[] = {0x1b, 0x37, 0x07, 0x64, 0x64};
    const auto header = m220RasterHeader(image.width, image.height);
    const uint8_t feed[] = {0x1b, 0x4a, 0x20};
    if (!send(init, sizeof(init)) || !send(density, sizeof(density)) ||
        !send(header.data(), header.size()) || !send(image.pixels, image.length) ||
        !send(feed, sizeof(feed))) { fail("M220 write failed or disconnected."); break; }
    ok = true;
  } while (false);
  if (client->isConnected()) client->disconnect();
  delete client;
  return ok;
}
