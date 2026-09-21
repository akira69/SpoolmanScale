#include "services/phomemo_m_series.h"
#include "services/phomemo_m_series_protocol.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_heap_caps.h>
#include <esp_gattc_api.h>
#include <freertos/queue.h>
#include <cstdio>
#include <cstring>

#include "lang.h"
#include "services/breadcrumb.h"

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

class MSeriesScanCollector : public BLEAdvertisedDeviceCallbacks {
 public:
  MSeriesScanCollector(LabelPrinterDevice* devices, size_t max_devices,
                       const LabelPrinterConfig& config)
      : out(devices), capacity(max_devices), selected(config) {}

  void onResult(BLEAdvertisedDevice device) override {
    LabelPrinterDevice candidate{};
    snprintf(candidate.name, sizeof(candidate.name), "%s", device.getName().c_str());
    snprintf(candidate.address, sizeof(candidate.address), "%s", device.getAddress().toString().c_str());
    if (selected.address[0] && !strcmp(candidate.address, selected.address)) selected_found = true;
    labelPrinterConsiderDevice(out, &count, capacity, candidate, selected);
  }

  size_t count = 0;
  bool selectedFound() const { return selected_found; }

 private:
  LabelPrinterDevice* out;
  size_t capacity;
  const LabelPrinterConfig& selected;
  bool selected_found = false;
};
}

size_t phomemoMSeriesScan(LabelPrinterDevice* out, size_t capacity,
                        const LabelPrinterConfig& selected, LabelPrinterProgressFn progress) {
  BLEDevice::init("");
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->clearResults();
  MSeriesScanCollector collector(out, capacity, selected);
  // With duplicate filtering the library keeps the first packet for each
  // address and drops later scan responses, which may carry the printer name.
  scan->setAdvertisedDeviceCallbacks(&collector, true);
  for (unsigned interval = 0; interval < 8; ++interval) {
    scan->start(1, false);
    if (progress) progress();
    if (labelPrinterConfigured(selected) && collector.selectedFound()) break;
  }
  scan->setAdvertisedDeviceCallbacks(nullptr);
  scan->clearResults();
  // Keep controller memory reusable for the next scan or print.
  BLEDevice::deinit(false);
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
  BLEDevice::init("");
  crumbSet("label printer BLE ready");
  BLEClient* client = BLEDevice::createClient();
  if (!s_disconnect_events) s_disconnect_events = xQueueCreate(1, sizeof(uint8_t));
  if (!s_connect_events) s_connect_events = xQueueCreate(2, sizeof(uint8_t));
  if (!s_disconnect_events || !s_connect_events) {
    delete client;
    BLEDevice::deinit(false);
    return fail("BLE queue unavailable.");
  }
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
  bool mtu_requested = false;
  do {
    if (!client->connect(BLEAddress(address))) {
      // REG/OPEN give their semaphores before BLEDevice calls our hook.
      // REG is event 0; even a synchronous registration error is ambiguous
      // with a failed REG callback, so wait and retain on timeout.
      const uint8_t expected = client->getConnId() != ESP_GATT_IF_NONE
          ? ESP_GATTC_OPEN_EVT : ESP_GATTC_REG_EVT;
      connect_completed = connectEventComplete(expected);
      fail(T(STR_LABEL_PRINTER_CONNECT_RETRY));
      break;
    }
    connected = true;
    s_gatt_if = client->getGattcIf();
    s_conn_id = client->getConnId();
    mtu_requested = client->setMTU(PHOMEMO_PREFERRED_MTU);
    if (mtu_requested) delay(200);
    BLERemoteService* service = client->getService(BLEUUID((uint16_t)0xff00));
    BLERemoteCharacteristic* write = service ? service->getCharacteristic(BLEUUID((uint16_t)0xff02)) : nullptr;
    if (!write || (!write->canWrite() && !write->canWriteNoResponse())) {
      fail("write characteristic missing."); break;
    }
    if (!s_write_events) s_write_events = xQueueCreate(1, sizeof(esp_gatt_status_t));
    if (!s_write_events) { fail("write queue unavailable."); break; }
    s_write_handle = write->getHandle();
    const size_t chunk = phomemoWriteChunk(client->getMTU());
    const bool response = !write->canWriteNoResponse();
    Serial.printf("%s BLE connected: MTU=%u requested=%u chunk=%u response=%u raster=%ux%u (%u bytes) heap=%u largest=%u psram=%u\n", profile.name,
                  client->getMTU(), unsigned(mtu_requested), unsigned(chunk), unsigned(response),
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
        xQueueReset(s_write_events);
        esp_err_t result = esp_ble_gattc_write_char(
            client->getGattcIf(), client->getConnId(), write->getHandle(),
            min(chunk, length - pos), const_cast<uint8_t*>(data + pos),
            response ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP,
            ESP_GATT_AUTH_REQ_NONE);
        if (result != ESP_OK) {
          Serial.printf("%s %s enqueue failed at %u/%u: %d\n", profile.name, stage, unsigned(pos), unsigned(length), int(result));
          return false;
        }
        esp_gatt_status_t status;
        if (xQueueReceive(s_write_events, &status, pdMS_TO_TICKS(3000)) != pdTRUE) {
          Serial.printf("%s %s write timeout at %u/%u, connected=%u\n", profile.name, stage, unsigned(pos), unsigned(length), unsigned(client->isConnected()));
          return false;
        }
        if (status != ESP_GATT_OK) {
          Serial.printf("%s %s GATT status %d at %u/%u\n", profile.name, stage, int(status), unsigned(pos), unsigned(length));
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
    return fail("disconnect timed out. Restart scale before retrying.");
  }
  delete client;
  BLEDevice::deinit(false);
  return ok;
}
