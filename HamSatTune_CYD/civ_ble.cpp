#include "civ_ble.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLERemoteCharacteristic.h>
#include <freertos/stream_buffer.h>

namespace {
const BLEUUID serviceUuid("14cf8001-1ec2-d408-1b04-2eb270f14203");
const BLEUUID dataUuid("14cf8002-1ec2-d408-1b04-2eb270f14203");
// IC-705 BLE CI-V sign-in. The ID must be 36 ASCII bytes and the name 16.
constexpr uint8_t id[] = {0xFE, 0xF1, 0x00, 0x61,
    'c','3','e','7','e','5','4','0','-','1','b','8','5','-','4','2','5','0','-',
    '9','c','f','6','-','1','b','3','2','a','b','7','0','5','d','2','0',0xFD};
constexpr uint8_t name[] = {0xFE, 0xF1, 0x00, 0x62,
    'H','a','m','S','a','t','T','u','n','e','-','C','Y','D',' ',' ',0xFD};
constexpr uint8_t token[] = {0xFE,0xF1,0x00,0x63,0xEE,0x39,0x09,0x10,0xFD};
static_assert(sizeof(id) == 41 && sizeof(name) == 21);
StreamBufferHandle_t incoming = nullptr;
BLEClient *client = nullptr;
BLERemoteCharacteristic *characteristic = nullptr;
volatile bool granted = false;
uint8_t grantWindow[5]{};

void notify(BLERemoteCharacteristic *, uint8_t *bytes, size_t length, bool) {
  for (size_t i = 0; i < length; ++i) {
    memmove(grantWindow, grantWindow + 1, 4);
    grantWindow[4] = bytes[i];
    if (grantWindow[0] == 0xFE && grantWindow[1] == 0xF1 &&
        grantWindow[2] == 0x00 && grantWindow[3] == 0x64 &&
        grantWindow[4] == 0xFD) granted = true;
  }
  if (incoming && xStreamBufferSpacesAvailable(incoming) >= length)
    xStreamBufferSend(incoming, bytes, length, 0);
}

void worker(void *) {
  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(800);
  scan->setWindow(240);
  client = BLEDevice::createClient();
  for (;;) {
    granted = false;
    characteristic = nullptr;
    if (incoming) xStreamBufferReset(incoming);
    BLEScanResults *results = scan->start(3, false);
    bool found = false;
    BLEAdvertisedDevice radio;
    if (results) {
      for (int i = 0; i < results->getCount(); ++i) {
        BLEAdvertisedDevice device = results->getDevice(i);
        if (device.haveServiceUUID() && device.getServiceUUID().equals(serviceUuid)) {
          radio = device;
          found = true;
          break;
        }
      }
    }
    scan->clearResults();
    if (!found) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    Serial.println("IC-705 BLE found; connecting.");
    if (!client || !client->connect(&radio)) {
      Serial.println("IC-705 BLE connection failed.");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    client->setMTU(517);
    BLERemoteService *service = client->getService(serviceUuid);
    characteristic = service ? service->getCharacteristic(dataUuid) : nullptr;
    if (!characteristic || !characteristic->canWrite() ||
        !characteristic->canNotify()) {
      Serial.println("IC-705 BLE CI-V service unavailable.");
      client->disconnect();
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    characteristic->registerForNotify(notify);
    delay(100);
    bool sent = characteristic->writeValue(const_cast<uint8_t *>(id), sizeof(id), true);
    delay(40);
    sent &= characteristic->writeValue(const_cast<uint8_t *>(name), sizeof(name), true);
    delay(40);
    sent &= characteristic->writeValue(const_cast<uint8_t *>(token), sizeof(token), true);
    if (!sent) Serial.println("IC-705 BLE sign-in write failed.");
    const uint32_t started = millis();
    while (sent && client->isConnected() && !granted &&
           millis() - started < 5000) vTaskDelay(pdMS_TO_TICKS(30));
    if (granted) Serial.println("IC-705 BLE CI-V connected.");
    else Serial.println("IC-705 BLE CI-V access unavailable; use Pairing Reception.");
    while (granted && client->isConnected())
      vTaskDelay(pdMS_TO_TICKS(200));
    granted = false;
    characteristic = nullptr;
    if (client->isConnected()) client->disconnect();
    vTaskDelay(pdMS_TO_TICKS(1500));
  }
}
}  // namespace

bool civBleBegin() {
  if (!BLEDevice::init("HamSatTune-CYD")) {
    Serial.println("IC-705 BLE init failed; check free memory.");
    return false;
  }
  incoming = xStreamBufferCreate(2048, 1);
  if (!incoming || xTaskCreatePinnedToCore(worker, "civ-ble", 6144,
                                          nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("IC-705 BLE task allocation failed.");
    return false;
  }
  Serial.println("IC-705 BLE CI-V scanning; use Pairing Reception on radio.");
  return true;
}
bool civBleConnected() { return granted && client && client->isConnected(); }
int civBleAvailable() { return incoming ? xStreamBufferBytesAvailable(incoming) : 0; }
int civBleRead() {
  uint8_t byte = 0;
  return incoming && xStreamBufferReceive(incoming, &byte, 1, 0) == 1 ? byte : -1;
}
size_t civBleWrite(const uint8_t *data, size_t length) {
  if (!civBleConnected() || !characteristic) return 0;
  return characteristic->writeValue(const_cast<uint8_t *>(data), length, false)
             ? length : 0;
}
