// Shared state of the host-side Arduino fakes: a chronological log of
// everything the library did on the bus (and every delay it requested).
#ifndef MOCK_BUS_H
#define MOCK_BUS_H

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace mock {

struct Event {
  enum Kind {
    WRITE,  // A completed write transaction (beginTransmission ... endTransmission)
    READ,   // A requestFrom() call, `data` holds the bytes the fake device returned
    DELAY,  // delay() or delayMicroseconds(), `micros` holds the duration
  };

  Kind kind;
  uint8_t address;
  std::vector<uint8_t> data;
  size_t requested;  // READ only: number of bytes the library asked for
  uint32_t micros;   // DELAY only
};

// The global event log. Cleared by TwoWire::reset().
std::vector<Event>& events();

}  // namespace mock
#endif
