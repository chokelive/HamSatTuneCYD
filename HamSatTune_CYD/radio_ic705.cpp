#include "radio_ic705.h"

#include <BluetoothSerial.h>
#include <cmath>
#include <cstring>

#include "civ_codec.h"

namespace {
constexpr uint8_t RADIO_ADDRESS = 0xA4;
constexpr uint8_t CONTROLLER_ADDRESS = 0xE0;
BluetoothSerial bluetooth;
SemaphoreHandle_t stateMutex = nullptr;
RadioSnapshot state;

enum class Command : uint8_t {
  None, ReadPtt, ReadRx, ReadTx, SelectA, Split, ModeRx, ModeTx, WriteRx, WriteTx
};
Command waiting = Command::None;
uint32_t waitingSince = 0;
uint32_t lastPttPoll = 0;
uint32_t lastPttReply = 0;
uint32_t lastRxPoll = 0;
uint32_t lastTxPoll = 0;
uint32_t lastRxSent = 0;
uint32_t lastTxSent = 0;
uint8_t setupStep = 0;
bool protocolActive = false;
uint8_t frame[32]{};
size_t frameLength = 0;

void setLink(RadioLinkState link) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (state.link != link) {
    state.link = link;
    ++state.version;
  }
  if (link != RadioLinkState::Connected) {
    state.control = false;
    state.pttKnown = false;
    state.rxKnown = false;
    state.txKnown = false;
  }
  xSemaphoreGive(stateMutex);
}

void disarm() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (state.control) {
    state.control = false;
    ++state.version;
  }
  xSemaphoreGive(stateMutex);
  setupStep = 0;
  lastRxSent = 0;
  lastTxSent = 0;
}

void initializeBluetooth() {
  // The IC-705 searches for and opens the data-device SPP connection.
  if (!bluetooth.begin("HamSatTune-CYD", false)) {
    setLink(RadioLinkState::Error);
    return;
  }
  bluetooth.setPin("0000", 4);
  bluetooth.onConfirmRequest([](uint32_t passkey) {
    Serial.printf("Bluetooth pairing passkey: %06lu; confirm it on the IC-705.\n",
                  static_cast<unsigned long>(passkey));
    // The IC-705 is the initiating device; confirm the pairing on its screen.
    bluetooth.confirmReply(true);
  });
  bluetooth.onAuthComplete([](bool success) {
    Serial.printf("Bluetooth authentication %s.\n",
                  success ? "succeeded" : "failed");
  });
}

void sendCommand(const uint8_t *payload, size_t length, Command command) {
  uint8_t packet[18] = {0xFE, 0xFE, RADIO_ADDRESS, CONTROLLER_ADDRESS};
  memcpy(packet + 4, payload, length);
  packet[4 + length] = 0xFD;
  if (bluetooth.write(packet, length + 5) != length + 5) {
    disarm();
    return;
  }
  waiting = command;
  waitingSince = millis();
}

void sendFrequency(uint8_t vfo, uint32_t hz, Command command) {
  uint8_t payload[7] = {0x25, vfo};
  civ::encodeFrequency(hz, payload + 2);
  sendCommand(payload, sizeof(payload), command);
}

bool modeBytes(const char *mode, bool receive, uint8_t &modeCode,
               uint8_t &dataMode) {
  dataMode = strstr(mode, "-D") != nullptr ? 1 : 0;
  if (strcmp(mode, "CW") == 0) {
    modeCode = receive ? 0x01 : 0x03;  // HamSatTune receives CW via USB.
    return true;
  }
  if (strncmp(mode, "USB", 3) == 0) modeCode = 0x01;
  else if (strncmp(mode, "LSB", 3) == 0) modeCode = 0x00;
  else if (strcmp(mode, "FM") == 0) modeCode = 0x05;
  else return false;
  return true;
}

void handleFrame(size_t length) {
  if (length < 6 || frame[0] != 0xFE || frame[1] != 0xFE ||
      frame[2] != CONTROLLER_ADDRESS || frame[3] != RADIO_ADDRESS ||
      frame[length - 1] != 0xFD) return;
  const uint8_t command = frame[4];
  if (command == 0xFA) {
    if (waiting != Command::None) {
      Serial.println("IC-705 rejected CI-V command.");
      disarm();
      waiting = Command::None;
    }
    return;
  }
  if (command == 0xFB) {
    if (waiting >= Command::SelectA && waiting <= Command::ModeTx) {
      ++setupStep;
    }
    if (waiting != Command::None) waiting = Command::None;
    return;
  }
  if (command == 0x1C && length >= 8 && frame[5] == 0x00) {
    const bool transmitting = frame[6] != 0x00;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    if (!state.pttKnown || state.transmitting != transmitting) ++state.version;
    state.pttKnown = true;
    state.transmitting = transmitting;
    xSemaphoreGive(stateMutex);
    lastPttReply = millis();
    if (waiting == Command::ReadPtt) waiting = Command::None;
  } else if (command == 0x25 && length >= 12 && frame[5] <= 1) {
    uint32_t frequency;
    if (!civ::decodeFrequency(frame + 6, frequency)) return;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    if (frame[5] == 0) {
      if (!state.rxKnown || state.rxHz != frequency) ++state.version;
      state.rxHz = frequency;
      state.rxKnown = true;
    } else {
      if (!state.txKnown || state.txHz != frequency) ++state.version;
      state.txHz = frequency;
      state.txKnown = true;
    }
    xSemaphoreGive(stateMutex);
    if ((frame[5] == 0 && waiting == Command::ReadRx) ||
        (frame[5] == 1 && waiting == Command::ReadTx)) waiting = Command::None;
  }
}

void readFrames() {
  while (bluetooth.available()) {
    const uint8_t value = bluetooth.read();
    if (frameLength == 0) {
      if (value == 0xFE) frame[frameLength++] = value;
      continue;
    }
    if (frameLength == 1 && value != 0xFE) {
      frameLength = 0;
      continue;
    }
    if (frameLength >= sizeof(frame)) frameLength = 0;
    frame[frameLength++] = value;
    if (value == 0xFD) {
      handleFrame(frameLength);
      frameLength = 0;
    }
  }
}
}  // namespace

void radioBegin() {
  stateMutex = xSemaphoreCreateMutex();
  state.link = RadioLinkState::Waiting;
  initializeBluetooth();
}

RadioSnapshot radioSnapshot() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  const RadioSnapshot result = state;
  xSemaphoreGive(stateMutex);
  return result;
}

void radioPreparePairing() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (state.link == RadioLinkState::Connected) {
    xSemaphoreGive(stateMutex);
    return;
  }
  xSemaphoreGive(stateMutex);
  disarm();
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  state.link = RadioLinkState::Waiting;
  ++state.version;
  xSemaphoreGive(stateMutex);
}

void radioToggleControl() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (state.link == RadioLinkState::Connected && state.pttKnown) {
    state.control = !state.control;
    ++state.version;
    setupStep = 0;
    lastRxSent = 0;
    lastTxSent = 0;
  }
  xSemaphoreGive(stateMutex);
}

void radioProfileChanged() { disarm(); }

void radioTick(const SatelliteProfile *profile, const TrackingResult &tracking) {
  RadioSnapshot snapshot = radioSnapshot();
  if (snapshot.link != RadioLinkState::Connected) {
    if (bluetooth.connected()) {
      // Accept the SPP link opened by the IC-705 as a data-device client.
      setLink(RadioLinkState::Connected);
      Serial.println("IC-705 Bluetooth SPP connected.");
      snapshot = radioSnapshot();
    } else {
      protocolActive = false;
      waiting = Command::None;
      return;
    }
  }
  if (!bluetooth.connected()) {
    setLink(RadioLinkState::Waiting);
    protocolActive = false;
    waiting = Command::None;
    return;
  }
  if (!protocolActive) {
    protocolActive = true;
    frameLength = 0;
    waiting = Command::None;
    setupStep = 0;
    lastPttPoll = lastRxPoll = lastTxPoll = 0;
    lastPttReply = lastRxSent = lastTxSent = 0;
  }
  readFrames();
  if (waiting != Command::None) {
    if (millis() - waitingSince < 1500) return;
    if (waiting >= Command::SelectA) {
      Serial.println("IC-705 CI-V write timed out; radio control disabled.");
      disarm();
    }
    waiting = Command::None;
  }

  const uint32_t now = millis();
  if (lastPttPoll == 0 || now - lastPttPoll >= 1000) {
    const uint8_t query[] = {0x1C, 0x00};
    sendCommand(query, sizeof(query), Command::ReadPtt);
    lastPttPoll = now;
    return;
  }
  if (snapshot.control) {
    if (!profile || strcmp(tracking.status, "TRACKING") != 0) {
      disarm();
      return;
    }
    if (!snapshot.pttKnown || snapshot.transmitting ||
        now - lastPttReply > 1500) return;
    if (setupStep < 4) {
      if (setupStep == 0) {
        const uint8_t selectA[] = {0x07, 0x00};
        sendCommand(selectA, sizeof(selectA), Command::SelectA);
      } else if (setupStep == 1) {
        const uint8_t split[] = {0x0F, profile->uplinkHz ? uint8_t(1) : uint8_t(0)};
        sendCommand(split, sizeof(split), Command::Split);
      } else if (setupStep == 2 || (setupStep == 3 && profile->uplinkHz)) {
        uint8_t mode, data;
        const bool rx = setupStep == 2;
        if (!modeBytes(rx ? profile->downlinkMode : profile->uplinkMode,
                       rx, mode, data)) {
          disarm();
          return;
        }
        const uint8_t modeCommand[] = {0x26, uint8_t(rx ? 0 : 1), mode, data};
        sendCommand(modeCommand, sizeof(modeCommand),
                    rx ? Command::ModeRx : Command::ModeTx);
      } else {
        setupStep = 4;
      }
      return;
    }
    const uint32_t targetRx = uint32_t(lround(tracking.receiveHz));
    const uint32_t targetTx = uint32_t(lround(tracking.transmitHz));
    if (targetRx != lastRxSent) {
      sendFrequency(0, targetRx, Command::WriteRx);
      lastRxSent = targetRx;
      return;
    }
    if (profile->uplinkHz && targetTx != lastTxSent) {
      sendFrequency(1, targetTx, Command::WriteTx);
      lastTxSent = targetTx;
      return;
    }
  }
  if (lastRxPoll == 0 || now - lastRxPoll >= 4000) {
    const uint8_t query[] = {0x25, 0x00};
    sendCommand(query, sizeof(query), Command::ReadRx);
    lastRxPoll = now;
  } else if (lastTxPoll == 0 || now - lastTxPoll >= 4000) {
    const uint8_t query[] = {0x25, 0x01};
    sendCommand(query, sizeof(query), Command::ReadTx);
    lastTxPoll = now;
  }
}

const char *radioMainStatus(const RadioSnapshot &snapshot) {
  if (snapshot.link == RadioLinkState::Connected)
    return snapshot.control ? "BT CONTROL" : "BT READ";
  if (snapshot.link == RadioLinkState::Waiting) return "BT WAIT";
  return "BT OFF";
}

const char *radioLinkText(RadioLinkState link) {
  switch (link) {
    case RadioLinkState::Waiting: return "WAITING FOR IC-705";
    case RadioLinkState::Connected: return "CONNECTED";
    case RadioLinkState::Disconnected: return "DISCONNECTED";
    case RadioLinkState::Error: return "BLUETOOTH ERROR";
    default: return "BLUETOOTH OFF";
  }
}
