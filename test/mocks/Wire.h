// Minimal host-side fake of the Arduino Wire (I2C) API used by the library.
//
// Like the Wire.h of the Arduino AVR core, this header deliberately does NOT
// include Arduino.h, so the library has to include what it uses.
#ifndef MOCK_WIRE_H
#define MOCK_WIRE_H

#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <vector>

#include "MockBus.h"

class TwoWire {
  public:
    // --- Arduino API ---------------------------------------------------------
    void begin(void) {}
    void beginTransmission(uint8_t address);
    size_t write(uint8_t data);
    size_t write(const uint8_t* data, size_t len);
    uint8_t endTransmission(void);
    uint8_t requestFrom(uint8_t address, size_t len);
    size_t readBytes(uint8_t* buffer, size_t len);

    // --- Test scripting ------------------------------------------------------
    // Forget all scripted behaviour and clear the global event log.
    void reset(void);
    // Queue the bytes the device returns on the next requestFrom(). If fewer
    // bytes than requested are queued the read is short; if nothing is queued
    // the device NACKs the read (zero bytes).
    void queueResponse(const std::vector<uint8_t>& bytes) { _responses.push_back(bytes); }
    // Queue the return value of the next endTransmission() (default: 0 = ACK).
    void queueEndTransmission(uint8_t status) { _endTransmissionResults.push_back(status); }
    // Size of the transmit buffer. write() refuses bytes beyond it, as the real
    // Wire implementation does.
    void setTxBufferSize(size_t size) { _txBufferSize = size; }

  private:
    uint8_t _txAddress = 0;
    std::vector<uint8_t> _txBuffer;
    size_t _txBufferSize = 32;
    std::vector<uint8_t> _rxBuffer;
    size_t _rxPos = 0;
    std::deque<std::vector<uint8_t> > _responses;
    std::deque<uint8_t> _endTransmissionResults;
};

extern TwoWire Wire;

#endif
