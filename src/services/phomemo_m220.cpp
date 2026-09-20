#include "services/phomemo_m220.h"
#include "services/phomemo_m220_protocol.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_heap_caps.h>
#include <esp_gattc_api.h>
#include <freertos/queue.h>
#include <cstdio>
#include <cstring>

namespace {
QueueHandle_t s_write_events = nullptr;
QueueHandle_t s_disconnect_events = nullptr;
QueueHandle_t s_connect_events = nullptr;
esp_gatt_if_t s_gatt_if = ESP_GATT_IF_NONE;
esp_gatt_if_t s_connect_gatt_if = ESP_GATT_IF_NONE;
uint16_t s_conn_id = 0, s_write_handle = 0;
uint16_t s_connect_app_id = 0;
bool s_client_unresolved = false;

void onGattEvent(esp_gattc_cb_event_t event, esp_gatt_if_t gatt_if,
                 esp_ble_gattc_cb_param_t* param) {
  if (event == ESP_GATTC_REG_EVT && s_connect_events &&
      param->reg.app_id == s_connect_app_id) {
    s_connect_gatt_if = gatt_if;
    uint8_t done = ESP_GATTC_REG_EVT;
    xQueueSend(s_connect_events, &done, 0);
  }
  if (event == ESP_GATTC_OPEN_EVT && s_connect_events &&
      gatt_if == s_connect_gatt_if) {
    uint8_t done = ESP_GATTC_OPEN_EVT;
    xQueueSend(s_connect_events, &done, 0);
  }
  if (event == ESP_GATTC_WRITE_CHAR_EVT && s_write_events &&
      gatt_if == s_gatt_if && param->write.conn_id == s_conn_id &&
      param->write.handle == s_write_handle) {
    esp_gatt_status_t status = param->write.status;
    xQueueSend(s_write_events, &status, 0);
  }
  if (event == ESP_GATTC_DISCONNECT_EVT && s_disconnect_events &&
      gatt_if == s_gatt_if && param->disconnect.conn_id == s_conn_id) {
    uint8_t done = 1;
    xQueueSend(s_disconnect_events, &done, 0);
  }
}

bool clientRegistered(BLEClient* client) {
  for (const auto& peer : BLEDevice::getPeerDevices(true))
    if (peer.second.peer_device == client) return true;
  return false;
}

bool connectEventComplete(uint8_t expected) {
  const uint32_t until = millis() + 3000;
  uint8_t event;
  while ((int32_t)(until - millis()) > 0)
    if (xQueueReceive(s_connect_events, &event, pdMS_TO_TICKS(100)) == pdTRUE &&
        event == expected) return true;
  return false;
}

class M220ScanCollector : public BLEAdvertisedDeviceCallbacks {
 public:
  M220ScanCollector(M220Device* devices, size_t max_devices)
      : out(devices), capacity(max_devices) {}

  void onResult(BLEAdvertisedDevice device) override {
    const std::string name = device.getName();
    // M-series printers can advertise their Q-prefixed serial instead of M220.
    if (name.find("M220") == std::string::npos &&
        !(name.size() >= 10 && name[0] == 'Q')) return;
    const std::string address = device.getAddress().toString();
    for (size_t i = 0; i < count; ++i)
      if (strcmp(out[i].address, address.c_str()) == 0) return;
    if (count >= capacity) return;
    snprintf(out[count].name, sizeof(out[count].name), "%s", name.c_str());
    snprintf(out[count].address, sizeof(out[count].address), "%s", address.c_str());
    ++count;
  }

  size_t count = 0;

 private:
  M220Device* out;
  size_t capacity;
};
}

size_t phomemoM220Scan(M220Device* out, size_t capacity) {
  BLEDevice::init("");
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->clearResults();
  M220ScanCollector collector(out, capacity);
  // With duplicate filtering the library keeps the first packet for each
  // address and drops later scan responses, which may carry the printer name.
  scan->setAdvertisedDeviceCallbacks(&collector, true);
  scan->start(8, false);
  scan->setAdvertisedDeviceCallbacks(nullptr);
  scan->clearResults();
  return collector.count;
}

bool phomemoM220Print(const char* address, const LabelRaster& image,
                     char* error, size_t error_size) {
  auto fail = [&](const char* message) { if (error_size) snprintf(error, error_size, "%s", message); return false; };
  if (error_size) error[0] = 0;
  if (!address || !*address) return fail("Select an M220 printer first.");
  if (!labelRasterPaddingValid(image)) return fail("Invalid label image.");
  if (image.width > M220_MAX_RASTER_WIDTH) return fail("M220 width exceeds print head.");
  if (s_client_unresolved) return fail("M220 BLE cleanup pending. Restart scale before retrying.");
  Serial.printf("M220 before client: heap=%u largest=%u psram=%u\n",
                unsigned(ESP.getFreeHeap()),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                unsigned(ESP.getFreePsram()));
  BLEDevice::init("");
  BLEClient* client = BLEDevice::createClient();
  if (!s_disconnect_events) s_disconnect_events = xQueueCreate(1, sizeof(uint8_t));
  if (!s_connect_events) s_connect_events = xQueueCreate(2, sizeof(uint8_t));
  if (!s_disconnect_events || !s_connect_events) { delete client; return fail("M220 BLE queue unavailable."); }
  xQueueReset(s_disconnect_events);
  xQueueReset(s_connect_events);
  s_gatt_if = ESP_GATT_IF_NONE;
  s_connect_gatt_if = ESP_GATT_IF_NONE;
  s_connect_app_id = BLEDevice::m_appId;
  s_conn_id = 0;
  BLEDevice::setCustomGattcHandler(onGattEvent);
  bool ok = false;
  bool connected = false;
  bool connect_completed = true;
  do {
    if (!client->connect(BLEAddress(address))) {
      // REG/OPEN give their semaphores before BLEDevice calls our hook.
      // REG is event 0; even a synchronous registration error is ambiguous
      // with a failed REG callback, so wait and retain on timeout.
      const uint8_t expected = client->getConnId() != ESP_GATT_IF_NONE
          ? ESP_GATTC_OPEN_EVT : ESP_GATTC_REG_EVT;
      connect_completed = connectEventComplete(expected);
      fail("Could not connect to M220.");
      break;
    }
    connected = true;
    s_gatt_if = client->getGattcIf();
    s_conn_id = client->getConnId();
    BLERemoteService* service = client->getService(BLEUUID((uint16_t)0xff00));
    BLERemoteCharacteristic* write = service ? service->getCharacteristic(BLEUUID((uint16_t)0xff02)) : nullptr;
    if (!write || (!write->canWrite() && !write->canWriteNoResponse())) {
      fail("M220 write characteristic missing."); break;
    }
    if (!s_write_events) s_write_events = xQueueCreate(1, sizeof(esp_gatt_status_t));
    if (!s_write_events) { fail("M220 write queue unavailable."); break; }
    s_write_handle = write->getHandle();
    const size_t chunk = m220WriteChunk(client->getMTU());
    const bool response = write->canWrite();
    Serial.printf("M220 BLE connected: MTU=%u chunk=%u response=%u raster=%ux%u (%u bytes) heap=%u largest=%u psram=%u\n",
                  client->getMTU(), unsigned(chunk), unsigned(response),
                  unsigned(image.width), unsigned(image.height), unsigned(image.length),
                  unsigned(ESP.getFreeHeap()),
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                  unsigned(ESP.getFreePsram()));
    auto send = [&](const char* stage, const uint8_t* data, size_t length) {
      for (size_t pos = 0; pos < length; pos += chunk) {
        if (!client->isConnected()) {
          Serial.printf("M220 %s disconnected at %u/%u\n", stage, unsigned(pos), unsigned(length));
          return false;
        }
        xQueueReset(s_write_events);
        esp_err_t result = esp_ble_gattc_write_char(
            client->getGattcIf(), client->getConnId(), write->getHandle(),
            min(chunk, length - pos), const_cast<uint8_t*>(data + pos),
            response ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP,
            ESP_GATT_AUTH_REQ_NONE);
        if (result != ESP_OK) {
          Serial.printf("M220 %s enqueue failed at %u/%u: %d\n", stage, unsigned(pos), unsigned(length), int(result));
          return false;
        }
        esp_gatt_status_t status;
        if (xQueueReceive(s_write_events, &status, pdMS_TO_TICKS(3000)) != pdTRUE) {
          Serial.printf("M220 %s write timeout at %u/%u, connected=%u\n", stage, unsigned(pos), unsigned(length), unsigned(client->isConnected()));
          return false;
        }
        if (status != ESP_GATT_OK) {
          Serial.printf("M220 %s GATT status %d at %u/%u\n", stage, int(status), unsigned(pos), unsigned(length));
          return false;
        }
        if (!client->isConnected()) {
          Serial.printf("M220 %s disconnected after %u/%u\n", stage, unsigned(pos), unsigned(length));
          return false;
        }
        delay(20);
        if (!strcmp(stage, "raster") && pos && pos % 4096 < chunk)
          Serial.printf("M220 raster %u/%u heap=%u\n", unsigned(pos), unsigned(length), unsigned(ESP.getFreeHeap()));
      }
      Serial.printf("M220 %s sent %u bytes, heap=%u largest=%u\n", stage, unsigned(length),
                    unsigned(ESP.getFreeHeap()),
                    unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
      return true;
    };
    const uint8_t init[] = {0x1b, 0x40};
    const uint8_t density[] = {0x1b, 0x37, 0x07, 0x64, 0x64};
    const auto header = m220RasterHeader(image.width, image.height);
    const uint8_t feed[] = {0x1b, 0x4a, 0x20};
    if (!send("init", init, sizeof(init)) || !send("density", density, sizeof(density)) ||
        !send("header", header.data(), header.size()) || !send("raster", image.pixels, image.length) ||
        !send("feed", feed, sizeof(feed))) { fail("M220 write failed or disconnected."); break; }
    ok = true;
  } while (false);
  if (client->isConnected()) client->disconnect();
  uint8_t done = 0;
  // The library removes the peer before its event handler returns. Our hook
  // runs after that handler, so only this signal permits deleting the client.
  const bool disconnect_done = connected &&
      xQueueReceive(s_disconnect_events, &done, pdMS_TO_TICKS(3000)) == pdTRUE;
  const bool still_registered = clientRegistered(client);
  BLEDevice::setCustomGattcHandler(nullptr);
  if ((connected && !disconnect_done) || !connect_completed || still_registered) {
    // ponytail: retain one unresolved client and require restart; a late GATT
    // callback could otherwise use freed memory.
    s_client_unresolved = true;
    if (!connected) return false;
    return fail("M220 disconnect timed out. Restart scale before retrying.");
  }
  delete client;
  return ok;
}
