#include "Arduino.h"
#include "Wire.h"

namespace mock {

std::vector<Event>& events() {
  static std::vector<Event> log;
  return log;
}

static void logDelay(uint32_t micros) {
  Event event = Event();
  event.kind = Event::DELAY;
  event.micros = micros;
  events().push_back(event);
}

}  // namespace mock

void delay(unsigned long ms) { mock::logDelay(ms * 1000UL); }

void delayMicroseconds(unsigned int us) { mock::logDelay(us); }

TwoWire Wire;

void TwoWire::reset(void) {
  *this = TwoWire();
  mock::events().clear();
}

void TwoWire::beginTransmission(uint8_t address) {
  _txAddress = address;
  _txBuffer.clear();
}

size_t TwoWire::write(uint8_t data) { return this->write(&data, 1); }

size_t TwoWire::write(const uint8_t* data, size_t len) {
  size_t written = 0;
  while (written < len && _txBuffer.size() < _txBufferSize) {
    _txBuffer.push_back(data[written++]);
  }
  return written;
}

uint8_t TwoWire::endTransmission(void) {
  mock::Event event = mock::Event();
  event.kind = mock::Event::WRITE;
  event.address = _txAddress;
  event.data = _txBuffer;
  mock::events().push_back(event);

  uint8_t status = 0;
  if (!_endTransmissionResults.empty()) {
    status = _endTransmissionResults.front();
    _endTransmissionResults.pop_front();
  }
  return status;
}

uint8_t TwoWire::requestFrom(uint8_t address, size_t len) {
  _rxBuffer.clear();
  _rxPos = 0;
  if (!_responses.empty()) {
    _rxBuffer = _responses.front();
    _responses.pop_front();
    if (_rxBuffer.size() > len) {
      _rxBuffer.resize(len);
    }
  }

  mock::Event event = mock::Event();
  event.kind = mock::Event::READ;
  event.address = address;
  event.data = _rxBuffer;
  event.requested = len;
  mock::events().push_back(event);
  return (uint8_t)_rxBuffer.size();
}

size_t TwoWire::readBytes(uint8_t* buffer, size_t len) {
  size_t count = 0;
  while (count < len && _rxPos < _rxBuffer.size()) {
    buffer[count++] = _rxBuffer[_rxPos++];
  }
  return count;
}
