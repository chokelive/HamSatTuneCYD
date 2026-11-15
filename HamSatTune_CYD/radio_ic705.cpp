#include "radio_ic705.h"

#include <cmath>
#include <cstring>

#include "civ_ble.h"
#include "civ_codec.h"

namespace {
constexpr uint8_t RADIO_ADDRESS = 0xA4;
constexpr uint8_t CONTROLLER_ADDRESS = 0xE0;
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
uint32_t lastDialChange = 0;
uint32_t rxReadExpectedHz = 0;
uint32_t dialReadHz = 0;
bool dialReadReady = false;
uint8_t setupStep = 0;
bool protocolActive = false;
uint32_t profileChangeAt = 0;
uint32_t profileSettleMs = 300;
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
    state.fault = false;
    state.pttKnown = false;
    state.rxKnown = false;
    state.txKnown = false;
    state.tuneOffsetHz = 0;
  }
  xSemaphoreGive(stateMutex);
}

void disarm(bool fault = false) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (state.control || state.tuneOffsetHz != 0 ||
      (fault && !state.fault)) {
    state.control = false;
    state.tuneOffsetHz = 0;
    if (fault) state.fault = true;
    ++state.version;
  }
  xSemaphoreGive(stateMutex);
  setupStep = 0;
  lastRxSent = 0;
  lastTxSent = 0;
  lastDialChange = 0;
  rxReadExpectedHz = 0;
  dialReadReady = false;
  waiting = Command::None;
}

void initializeBluetooth() {
  // The CYD connects to the IC-705 BLE CI-V server.
  if (!civBleBegin()) {
    Serial.println("IC-705 CI-V BLE start failed.");
    setLink(RadioLinkState::Error);
    return;
  }
}

void sendCommand(const uint8_t *payload, size_t length, Command command) {
  uint8_t packet[18] = {0xFE, 0xFE, RADIO_ADDRESS, CONTROLLER_ADDRESS};
  memcpy(packet + 4, payload, length);
  packet[4 + length] = 0xFD;
  if (civBleWrite(packet, length + 5) != length + 5) {
    disarm(true);
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
      disarm(true);
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
    if (frame[5] == 0 && waiting == Command::ReadRx) {
      dialReadHz = frequency;
      dialReadReady = true;
      lastRxPoll = millis();  // Leave a command slot after every reply.
    }
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
  while (civBleAvailable()) {
    const uint8_t value = civBleRead();
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

void radioProfileChanged() {
  // A delayed ACK from the old profile must not advance the new setup.
  profileSettleMs = waiting >= Command::SelectA ? 1500 : 300;
  disarm();
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (state.fault) {
    state.fault = false;
    ++state.version;
  }
  xSemaphoreGive(stateMutex);
  frameLength = 0;
  profileChangeAt = millis();
}

void radioCenterTune() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (state.tuneOffsetHz != 0) {
    state.tuneOffsetHz = 0;
    ++state.version;
    lastRxSent = 0;
    lastTxSent = 0;
    lastDialChange = 0;
  }
  xSemaphoreGive(stateMutex);
  dialReadReady = false;
  rxReadExpectedHz = 0;
}

void radioTick(const SatelliteProfile *profile, const TrackingResult &tracking) {
  RadioSnapshot snapshot = radioSnapshot();
  if (snapshot.link != RadioLinkState::Connected) {
    if (civBleConnected()) {
      setLink(RadioLinkState::Connected);
      Serial.println("IC-705 BLE CI-V ready for tuning.");
      snapshot = radioSnapshot();
    } else {
      protocolActive = false;
      waiting = Command::None;
      return;
    }
  }
  if (!civBleConnected()) {
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
  snapshot = radioSnapshot();
  if (dialReadReady) {
    dialReadReady = false;
    if (snapshot.control && profile &&
        trackingFreeTuneSupported(*profile) && setupStep >= 4 &&
        snapshot.pttKnown && !snapshot.transmitting &&
        rxReadExpectedHz != 0 &&
        tracking.tuneOffsetHz == snapshot.tuneOffsetHz) {
      const int32_t changeHz = int32_t(dialReadHz) -
                               int32_t(rxReadExpectedHz);
      // The expected frequency is captured at the read request, so the
      // high-elevation drift allowance used by desktop HamSatTune is not
      // needed here. Keep small dial steps responsive.
      const int32_t thresholdHz = 50;
      if (abs(changeHz) > thresholdHz) {
        const int32_t nextOffset = snapshot.tuneOffsetHz + changeHz;
        if (trackingFreeTuneAllowed(*profile, nextOffset)) {
          xSemaphoreTake(stateMutex, portMAX_DELAY);
          state.tuneOffsetHz = nextOffset;
          ++state.version;
          xSemaphoreGive(stateMutex);
          lastRxSent = dialReadHz;
          lastTxSent = 0;
          lastDialChange = millis();
          snapshot.tuneOffsetHz = nextOffset;
        } else {
          // Reject a dial position outside the published passbands and
          // restore the last valid target on the next control tick.
          lastRxSent = 0;
        }
      }
    }
  }
  if (waiting != Command::None) {
    if (millis() - waitingSince < 1500) return;
    if (waiting >= Command::SelectA) {
      Serial.println("IC-705 CI-V write timed out; radio control disabled.");
      disarm(true);
    }
    waiting = Command::None;
  }
  snapshot = radioSnapshot();

  const uint32_t now = millis();
  if (lastPttPoll == 0 || now - lastPttPoll >= 1000) {
    const uint8_t query[] = {0x1C, 0x00};
    sendCommand(query, sizeof(query), Command::ReadPtt);
    lastPttPoll = now;
    return;
  }
  if (!snapshot.control && !snapshot.fault && snapshot.pttKnown &&
      !snapshot.transmitting && now - lastPttReply <= 1500 && profile &&
      strcmp(tracking.status, "TRACKING") == 0 &&
      (profileChangeAt == 0 || now - profileChangeAt >= profileSettleMs)) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    state.control = true;
    ++state.version;
    xSemaphoreGive(stateMutex);
    setupStep = 0;
    lastRxSent = lastTxSent = 0;
    snapshot.control = true;
  }
  if (snapshot.control) {
    if (!profile || strcmp(tracking.status, "TRACKING") != 0) {
      disarm();
      return;
    }
    if (!snapshot.pttKnown || snapshot.transmitting ||
        now - lastPttReply > 1500) return;
    if (tracking.tuneOffsetHz != snapshot.tuneOffsetHz) return;
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
          disarm(true);
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
    // Keep observing the dial even when rapid Doppler updates also require
    // TX writes. Otherwise the write queue can starve RX readback.
    if (trackingFreeTuneSupported(*profile) && lastRxSent != 0 &&
        (lastRxPoll == 0 || now - lastRxPoll >= 300)) {
      const uint8_t query[] = {0x25, 0x00};
      rxReadExpectedHz = lastRxSent;
      sendCommand(query, sizeof(query), Command::ReadRx);
      lastRxPoll = now;
      return;
    }
    const uint32_t targetRx = uint32_t(lround(tracking.receiveHz));
    const uint32_t targetTx = uint32_t(lround(tracking.transmitHz));
    const bool dialMoving = trackingFreeTuneSupported(*profile) &&
                            lastDialChange != 0 &&
                            now - lastDialChange < 700;
    if (!dialMoving && targetRx != lastRxSent) {
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
  if (lastTxPoll == 0 || now - lastTxPoll >= 4000) {
    const uint8_t query[] = {0x25, 0x01};
    sendCommand(query, sizeof(query), Command::ReadTx);
    lastTxPoll = now;
  } else if (lastRxPoll == 0 || now - lastRxPoll >= 4000) {
    const uint8_t query[] = {0x25, 0x00};
    rxReadExpectedHz = lastRxSent;
    sendCommand(query, sizeof(query), Command::ReadRx);
    lastRxPoll = now;
  }
}

const char *radioMainStatus(const RadioSnapshot &snapshot) {
  if (snapshot.link == RadioLinkState::Connected)
    return snapshot.fault ? "BT ERROR" :
           snapshot.control ? (snapshot.tuneOffsetHz ? "BT FREE" : "BT CONTROL")
                            : "BT WAIT";
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
