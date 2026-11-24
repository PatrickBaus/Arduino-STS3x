// Host-based unit tests for the STS3x library. The library is compiled against
// the fake Arduino API in mocks/, which records all bus traffic and lets the
// tests script the answers of the sensor.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cmath>
#include <ostream>
#include <vector>

#include <Arduino.h>
#include <Wire.h>

#include "STS3x.h"

typedef std::vector<uint8_t> Bytes;
using mock::Event;

namespace doctest {
template <>
struct StringMaker<Bytes> {
  static String convert(const Bytes& bytes) {
    static const char* digits = "0123456789ABCDEF";
    String result = "{";
    for (size_t i = 0; i < bytes.size(); i++) {
      const char text[] = {' ', '0', 'x', digits[bytes[i] >> 4], digits[bytes[i] & 0x0F], '\0'};
      result += text;
    }
    return result + " }";
  }
};
}  // namespace doctest

namespace {

const uint8_t DEFAULT_ADDRESS = 0x4A;

// Independent reference implementation of the Sensirion CRC-8
// (polynomial 0x31, init 0xFF, no reflection, no final XOR).
uint8_t referenceCrc(const Bytes& data) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < data.size(); i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

// A 16 bit word as it travels over the bus: MSB, LSB, CRC
Bytes word(const uint16_t value) {
  Bytes result;
  result.push_back(value >> 8);
  result.push_back(value & 0xFF);
  result.push_back(referenceCrc(result));
  return result;
}

Bytes operator+(const Bytes& a, const Bytes& b) {
  Bytes result(a);
  result.insert(result.end(), b.begin(), b.end());
  return result;
}

Bytes corrupted(Bytes bytes, const size_t index) {
  bytes[index] ^= 0x01;
  return bytes;
}

Bytes bytes(uint8_t a, uint8_t b) {
  Bytes result;
  result.push_back(a);
  result.push_back(b);
  return result;
}

// The event log without the "no delay requested" entries
std::vector<Event> busLog() {
  std::vector<Event> result;
  for (size_t i = 0; i < mock::events().size(); i++) {
    const Event& event = mock::events()[i];
    if (event.kind != Event::DELAY || event.micros != 0) {
      result.push_back(event);
    }
  }
  return result;
}

void checkWrite(const Event& event, const Bytes& expected, const uint8_t address = DEFAULT_ADDRESS) {
  CHECK(event.kind == Event::WRITE);
  CHECK(event.address == address);
  CHECK(event.data == expected);
}

void checkRead(const Event& event, const size_t expectedLength, const uint8_t address = DEFAULT_ADDRESS) {
  CHECK(event.kind == Event::READ);
  CHECK(event.address == address);
  CHECK(event.requested == expectedLength);
}

void checkDelayAtLeast(const Event& event, const uint32_t micros) {
  CHECK(event.kind == Event::DELAY);
  CHECK(event.micros >= micros);
}

// A command without arguments followed by a read: write, optional wait, read
void checkQuery(const Bytes& command, const size_t readLength, const uint32_t minDelay = 0,
                const uint8_t address = DEFAULT_ADDRESS) {
  const std::vector<Event> log = busLog();
  REQUIRE(log.size() == (minDelay ? 3u : 2u));
  checkWrite(log[0], command, address);
  if (minDelay) {
    checkDelayAtLeast(log[1], minDelay);
  }
  checkRead(log.back(), readLength, address);
}

struct Fixture {
  Fixture() : sensor(Wire) { Wire.reset(); }
  STS3x sensor;
};

}  // namespace

TEST_CASE("the test's reference CRC matches the datasheet example") {
  // Datasheet: CRC(0xBEEF) = 0x92
  CHECK(referenceCrc(bytes(0xBE, 0xEF)) == 0x92);
  CHECK(word(0xBEEF) == bytes(0xBE, 0xEF) + Bytes(1, 0x92));
}

TEST_SUITE("conversion") {
  TEST_CASE("convertToCelsius implements T = -45 + 175 * raw / 65535") {
    CHECK(STS3x::convertToCelsius(0x0000) == doctest::Approx(-45.0));
    CHECK(STS3x::convertToCelsius(0xFFFF) == doctest::Approx(130.0));
    CHECK(STS3x::convertToCelsius(0x6666) == doctest::Approx(25.0));
    CHECK(STS3x::convertToCelsius(0x8000) == doctest::Approx(42.50134).epsilon(1e-6));
    // 0 degrees C is 45/175 of full scale
    CHECK(STS3x::convertToCelsius(16852) == doctest::Approx(0.0).scale(1.0).epsilon(2e-3));
  }

  TEST_CASE("convertToRaw is the inverse of convertToCelsius") {
    CHECK(STS3x::convertToRaw(-45.0f) == 0);
    CHECK(STS3x::convertToRaw(130.0f) == 65535);
    CHECK(STS3x::convertToRaw(25.0f) == 26214);
    CHECK(STS3x::convertToRaw(42.5f) == 32768);  // 32767.5 rounds up

    uint32_t mismatches = 0;
    for (uint32_t raw = 0; raw <= 0xFFFF; raw++) {
      if (STS3x::convertToRaw(STS3x::convertToCelsius((uint16_t)raw)) != raw) {
        mismatches++;
      }
    }
    CHECK(mismatches == 0);
  }

  TEST_CASE("convertToRaw saturates outside the measurement range") {
    CHECK(STS3x::convertToRaw(-45.01f) == 0);
    CHECK(STS3x::convertToRaw(-273.15f) == 0);
    CHECK(STS3x::convertToRaw(130.01f) == 65535);
    CHECK(STS3x::convertToRaw(1000.0f) == 65535);
    CHECK(STS3x::convertToRaw(NAN) == 0);
  }
}

TEST_SUITE("single shot measurement") {
  TEST_CASE_FIXTURE(Fixture, "readTemp defaults to high repeatability without clock stretching") {
    Wire.queueResponse(word(0x6666));
    CHECK(sensor.readTemp() == doctest::Approx(25.0));
    checkQuery(bytes(0x24, 0x00), 3, 15000);  // max. 15 ms conversion time
  }

  TEST_CASE_FIXTURE(Fixture, "readTemp with medium repeatability") {
    Wire.queueResponse(word(0x0000));
    CHECK(sensor.readTemp(REP_MEDIUM) == doctest::Approx(-45.0));
    checkQuery(bytes(0x24, 0x0B), 3, 6000);  // max. 6 ms conversion time
  }

  TEST_CASE_FIXTURE(Fixture, "readTemp with low repeatability") {
    Wire.queueResponse(word(0xFFFF));
    CHECK(sensor.readTemp(REP_LOW) == doctest::Approx(130.0));
    checkQuery(bytes(0x24, 0x16), 3, 4000);  // max. 4 ms conversion time
  }

  TEST_CASE_FIXTURE(Fixture, "readTempRaw returns the raw sensor word") {
    Wire.queueResponse(word(0xBEEF));
    CHECK(sensor.readTempRaw() == 0xBEEF);
    checkQuery(bytes(0x24, 0x00), 3, 15000);

    Wire.reset();
    Wire.queueResponse(word(0x1234));
    CHECK(sensor.readTempRaw(REP_MEDIUM) == 0x1234);
    checkQuery(bytes(0x24, 0x0B), 3, 6000);

    Wire.reset();
    Wire.queueResponse(word(0x00FF));
    CHECK(sensor.readTempRaw(REP_LOW) == 0x00FF);
    checkQuery(bytes(0x24, 0x16), 3, 4000);
  }

  TEST_CASE_FIXTURE(Fixture, "the configured I2C address is used") {
    STS3x other(Wire, 0x4B);
    Wire.queueResponse(word(0x6666));
    CHECK(other.readTempRaw() == 0x6666);
    checkQuery(bytes(0x24, 0x00), 3, 15000, 0x4B);
  }

  TEST_CASE_FIXTURE(Fixture, "readTemp returns NaN on a CRC error") {
    Wire.queueResponse(corrupted(word(0x6666), 2));  // wrong CRC
    CHECK(std::isnan(sensor.readTemp()));
    Wire.queueResponse(corrupted(word(0x6666), 0));  // wrong data
    CHECK(std::isnan(sensor.readTemp()));
  }

  TEST_CASE_FIXTURE(Fixture, "readTemp returns NaN if the sensor does not answer") {
    CHECK(std::isnan(sensor.readTemp()));  // read NACKed, no data
    Wire.queueResponse(bytes(0x66, 0x66));  // short read
    CHECK(std::isnan(sensor.readTemp()));
    // An all zero answer has an invalid CRC, it must not be read as -45 degrees C
    Wire.queueResponse(Bytes(3, 0x00));
    CHECK(std::isnan(sensor.readTemp()));
  }

  TEST_CASE_FIXTURE(Fixture, "an unknown repeatability is rejected without bus traffic") {
    CHECK(std::isnan(sensor.readTemp(static_cast<Repeatability>(3))));
    CHECK(sensor.readTempRaw(static_cast<Repeatability>(3)) == 0);
    CHECK(mock::events().empty());
  }
}

TEST_SUITE("periodic measurement") {
  TEST_CASE_FIXTURE(Fixture, "setContinousSampling sends the datasheet command for each mode") {
    struct Mode {
      MeasurementsPerSecond mps;
      Repeatability repeatability;
      uint8_t msb;
      uint8_t lsb;
    };
    const Mode modes[] = {
        {MPS_05, REP_HIGH, 0x20, 0x32}, {MPS_05, REP_MEDIUM, 0x20, 0x24}, {MPS_05, REP_LOW, 0x20, 0x2F},
        {MPS_1, REP_HIGH, 0x21, 0x30},  {MPS_1, REP_MEDIUM, 0x21, 0x26},  {MPS_1, REP_LOW, 0x21, 0x2D},
        {MPS_2, REP_HIGH, 0x22, 0x36},  {MPS_2, REP_MEDIUM, 0x22, 0x20},  {MPS_2, REP_LOW, 0x22, 0x2B},
        {MPS_4, REP_HIGH, 0x23, 0x34},  {MPS_4, REP_MEDIUM, 0x23, 0x22},  {MPS_4, REP_LOW, 0x23, 0x29},
        {MPS_10, REP_HIGH, 0x27, 0x37}, {MPS_10, REP_MEDIUM, 0x27, 0x21}, {MPS_10, REP_LOW, 0x27, 0x2A},
    };

    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
      CAPTURE(i);
      Wire.reset();
      sensor.setContinousSampling(modes[i].mps, modes[i].repeatability);
      const std::vector<Event> log = busLog();
      REQUIRE(log.size() == 1);
      checkWrite(log[0], bytes(modes[i].msb, modes[i].lsb));
    }
  }

  TEST_CASE_FIXTURE(Fixture, "setContinousSampling with values outside of the enums") {
    // The arguments are not validated. This documents the current behaviour:
    // the part of the command that cannot be looked up is left at zero.
    const MeasurementsPerSecond rates[] = {MPS_05, MPS_1, MPS_2, MPS_4, MPS_10};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
      CAPTURE(i);
      Wire.reset();
      sensor.setContinousSampling(rates[i], static_cast<Repeatability>(3));
      const std::vector<Event> log = busLog();
      REQUIRE(log.size() == 1);
      checkWrite(log[0], bytes((uint8_t)rates[i], 0x00));
    }

    Wire.reset();
    sensor.setContinousSampling(static_cast<MeasurementsPerSecond>(0x25), REP_HIGH);
    const std::vector<Event> log = busLog();
    REQUIRE(log.size() == 1);
    checkWrite(log[0], bytes(0x25, 0x00));
  }

  TEST_CASE_FIXTURE(Fixture, "fetchTemp reads the latest result") {
    Wire.queueResponse(word(0x6666));
    CHECK(sensor.fetchTemp() == doctest::Approx(25.0));
    checkQuery(bytes(0xE0, 0x00), 3);
  }

  TEST_CASE_FIXTURE(Fixture, "fetchTempRaw returns the raw sensor word") {
    Wire.queueResponse(word(0xABCD));
    CHECK(sensor.fetchTempRaw() == 0xABCD);
    checkQuery(bytes(0xE0, 0x00), 3);
  }

  TEST_CASE_FIXTURE(Fixture, "fetchTemp returns NaN if no (valid) data is available") {
    CHECK(std::isnan(sensor.fetchTemp()));  // The sensor NACKs the read when there is no new data
    Wire.queueResponse(corrupted(word(0x6666), 1));
    CHECK(std::isnan(sensor.fetchTemp()));
    Wire.queueResponse(Bytes(1, 0x66));
    CHECK(std::isnan(sensor.fetchTemp()));
  }

  TEST_CASE_FIXTURE(Fixture, "stopConversion sends the break command and waits 1 ms") {
    sensor.stopConversion();
    const std::vector<Event> log = busLog();
    REQUIRE(log.size() == 2);
    checkWrite(log[0], bytes(0x30, 0x93));
    checkDelayAtLeast(log[1], 1000);
  }
}

TEST_SUITE("housekeeping commands") {
  TEST_CASE_FIXTURE(Fixture, "reset sends the soft reset command and waits for the sensor") {
    sensor.reset();
    const std::vector<Event> log = busLog();
    REQUIRE(log.size() == 2);
    checkWrite(log[0], bytes(0x30, 0xA2));
    checkDelayAtLeast(log[1], 1000);
  }

  TEST_CASE_FIXTURE(Fixture, "setHeaterState switches the heater") {
    sensor.setHeaterState(true);
    checkWrite(busLog().at(0), bytes(0x30, 0x6D));

    Wire.reset();
    sensor.setHeaterState(false);
    checkWrite(busLog().at(0), bytes(0x30, 0x66));
  }

  TEST_CASE_FIXTURE(Fixture, "clearStatus sends the clear status command") {
    sensor.clearStatus();
    checkWrite(busLog().at(0), bytes(0x30, 0x41));
  }

  TEST_CASE_FIXTURE(Fixture, "readSerial reads two CRC protected words") {
    Wire.queueResponse(word(0x1234) + word(0x5678));
    CHECK(sensor.readSerial() == 0x12345678UL);
    checkQuery(bytes(0x37, 0x80), 6, 1000);

    Wire.reset();
    Wire.queueResponse(word(0xFEDC) + word(0xBA98));  // All upper bits set, catches sign extension
    CHECK(sensor.readSerial() == 0xFEDCBA98UL);
  }

  TEST_CASE_FIXTURE(Fixture, "readSerial returns 0 on errors") {
    CHECK(sensor.readSerial() == 0);  // NACK
    Wire.queueResponse(corrupted(word(0x1234) + word(0x5678), 2));  // CRC of the first word
    CHECK(sensor.readSerial() == 0);
    Wire.queueResponse(corrupted(word(0x1234) + word(0x5678), 5));  // CRC of the second word
    CHECK(sensor.readSerial() == 0);
    Wire.queueResponse(word(0x1234) + bytes(0x56, 0x78));  // short read
    CHECK(sensor.readSerial() == 0);
  }
}

TEST_SUITE("status register") {
  TEST_CASE_FIXTURE(Fixture, "readStatus decodes the status after power up") {
    // Datasheet: after reset the alert pending and the system reset flags are set (0x8010)
    Wire.queueResponse(word(0x8010));
    SensorStatus status;
    CHECK(sensor.readStatus(status));
    checkQuery(bytes(0xF3, 0x2D), 3);

    CHECK(status.alertPending);
    CHECK(status.systemReset);
    CHECK_FALSE(status.heaterEnabled);
    CHECK_FALSE(status.alertTemperature);
    CHECK_FALSE(status.commandError);
    CHECK_FALSE(status.checksumError);
  }

  TEST_CASE_FIXTURE(Fixture, "readStatus maps each flag to its register bit") {
    struct Flag {
      uint16_t mask;
      bool SensorStatus::*member;
    };
    const Flag flags[] = {
        {1u << 15, &SensorStatus::alertPending},     {1u << 13, &SensorStatus::heaterEnabled},
        {1u << 10, &SensorStatus::alertTemperature}, {1u << 4, &SensorStatus::systemReset},
        {1u << 1, &SensorStatus::commandError},      {1u << 0, &SensorStatus::checksumError},
    };
    const size_t count = sizeof(flags) / sizeof(flags[0]);

    uint16_t allFlags = 0;
    for (size_t i = 0; i < count; i++) {
      CAPTURE(i);
      allFlags |= flags[i].mask;
      Wire.queueResponse(word(flags[i].mask));
      SensorStatus status;
      CHECK(sensor.readStatus(status));
      for (size_t j = 0; j < count; j++) {
        CAPTURE(j);
        CHECK(status.*(flags[j].member) == (i == j));
      }
    }

    // The reserved bits must not leak into any flag
    Wire.queueResponse(word((uint16_t)~allFlags));
    SensorStatus status;
    CHECK(sensor.readStatus(status));
    for (size_t j = 0; j < count; j++) {
      CAPTURE(j);
      CHECK_FALSE(status.*(flags[j].member));
    }
  }

  TEST_CASE_FIXTURE(Fixture, "readStatus reports communication errors") {
    SensorStatus status;
    CHECK_FALSE(sensor.readStatus(status));  // NACK
    Wire.queueResponse(corrupted(word(0x8010), 2));
    CHECK_FALSE(sensor.readStatus(status));
    Wire.queueResponse(bytes(0x80, 0x10));
    CHECK_FALSE(sensor.readStatus(status));
  }
}

TEST_SUITE("alert limits") {
  // The limit register holds the 9 most significant bits of the temperature in
  // its bits 8:0.
  // 60 degrees C = 0x9999 raw -> 0x0133, 58 degrees C = 0x96AC raw -> 0x012D
  const uint16_t RAW_60C = 0x9999;
  const uint16_t RAW_58C = 0x96AC;

  TEST_CASE("the example limits match the conversion") {
    CHECK(STS3x::convertToRaw(60.0f) == RAW_60C);
    CHECK(STS3x::convertToRaw(58.0f) == RAW_58C);
  }

  TEST_CASE_FIXTURE(Fixture, "setAlertLimitsHigh writes set and clear limit with CRC") {
    CHECK(sensor.setAlertLimitsHigh(RAW_60C, RAW_58C));
    const std::vector<Event> log = busLog();
    REQUIRE(log.size() == 2);
    checkWrite(log[0], bytes(0x61, 0x1D) + word(0x0133));
    checkWrite(log[1], bytes(0x61, 0x16) + word(0x012D));
  }

  TEST_CASE_FIXTURE(Fixture, "setAlertLimitsLow writes set and clear limit with CRC") {
    CHECK(sensor.setAlertLimitsLow(0x0000, 0xFFFF));
    const std::vector<Event> log = busLog();
    REQUIRE(log.size() == 2);
    checkWrite(log[0], bytes(0x61, 0x00) + word(0x0000));
    checkWrite(log[1], bytes(0x61, 0x0B) + word(0x01FF));
  }

  TEST_CASE_FIXTURE(Fixture, "setAlertLimits* fail if the sensor does not acknowledge") {
    SUBCASE("high, set limit") {
      Wire.queueEndTransmission(2);  // address NACK
      CHECK_FALSE(sensor.setAlertLimitsHigh(RAW_60C, RAW_58C));
      CHECK(busLog().size() == 1);  // the clear limit is not written
    }
    SUBCASE("high, clear limit") {
      Wire.queueEndTransmission(0);
      Wire.queueEndTransmission(3);  // data NACK, e.g. during a measurement
      CHECK_FALSE(sensor.setAlertLimitsHigh(RAW_60C, RAW_58C));
      CHECK(busLog().size() == 2);
    }
    SUBCASE("low, set limit") {
      Wire.queueEndTransmission(2);
      CHECK_FALSE(sensor.setAlertLimitsLow(RAW_58C, RAW_60C));
      CHECK(busLog().size() == 1);
    }
    SUBCASE("low, clear limit") {
      Wire.queueEndTransmission(0);
      Wire.queueEndTransmission(4);
      CHECK_FALSE(sensor.setAlertLimitsLow(RAW_58C, RAW_60C));
      CHECK(busLog().size() == 2);
    }
  }

  TEST_CASE_FIXTURE(Fixture, "setAlertLimits* fail if the transmit buffer overflows") {
    // 5 bytes are needed: command (2), limit (2) and CRC (1)
    for (size_t size = 0; size < 5; size++) {
      CAPTURE(size);
      Wire.reset();
      Wire.setTxBufferSize(size);
      CHECK_FALSE(sensor.setAlertLimitsHigh(RAW_60C, RAW_58C));
      CHECK_FALSE(sensor.setAlertLimitsLow(RAW_58C, RAW_60C));
    }
    Wire.reset();
    Wire.setTxBufferSize(5);
    CHECK(sensor.setAlertLimitsHigh(RAW_60C, RAW_58C));
    CHECK(sensor.setAlertLimitsLow(RAW_58C, RAW_60C));
  }

  TEST_CASE_FIXTURE(Fixture, "readAlertLimitsHigh reads set and clear limit") {
    Wire.queueResponse(word(0x0133));
    Wire.queueResponse(word(0x012D));
    uint16_t set = 0;
    uint16_t clear = 0;
    CHECK(sensor.readAlertLimitsHigh(set, clear));
    CHECK(set == (RAW_60C & 0xFF80));
    CHECK(clear == (RAW_58C & 0xFF80));

    const std::vector<Event> log = busLog();
    REQUIRE(log.size() == 4);
    checkWrite(log[0], bytes(0xE1, 0x1F));
    checkRead(log[1], 3);
    checkWrite(log[2], bytes(0xE1, 0x14));
    checkRead(log[3], 3);
  }

  TEST_CASE_FIXTURE(Fixture, "readAlertLimitsLow reads set and clear limit") {
    Wire.queueResponse(word(0x0000));
    // The upper 7 bits are not part of the temperature limit and must be ignored
    Wire.queueResponse(word(0xFFFF));
    uint16_t set = 0xAAAA;
    uint16_t clear = 0;
    CHECK(sensor.readAlertLimitsLow(set, clear));
    CHECK(set == 0x0000);
    CHECK(clear == 0xFF80);

    const std::vector<Event> log = busLog();
    REQUIRE(log.size() == 4);
    checkWrite(log[0], bytes(0xE1, 0x02));
    checkRead(log[1], 3);
    checkWrite(log[2], bytes(0xE1, 0x09));
    checkRead(log[3], 3);
  }

  TEST_CASE_FIXTURE(Fixture, "written limits can be read back") {
    // Feed the words the library wrote back into it
    CHECK(sensor.setAlertLimitsHigh(RAW_60C, RAW_58C));
    std::vector<Event> log = busLog();
    REQUIRE(log.size() == 2);
    for (size_t i = 0; i < 2; i++) {
      REQUIRE(log[i].data.size() == 5);
      Wire.queueResponse(Bytes(log[i].data.begin() + 2, log[i].data.end()));
    }
    uint16_t set = 0;
    uint16_t clear = 0;
    CHECK(sensor.readAlertLimitsHigh(set, clear));
    CHECK(STS3x::convertToCelsius(set) == doctest::Approx(60.0).epsilon(0.01));  // 9 bit resolution
    CHECK(STS3x::convertToCelsius(clear) == doctest::Approx(58.0).epsilon(0.01));
  }

  TEST_CASE_FIXTURE(Fixture, "readAlertLimits* report communication errors") {
    uint16_t set = 0;
    uint16_t clear = 0;
    SUBCASE("high, set limit") {
      Wire.queueResponse(corrupted(word(0x0133), 2));
      CHECK_FALSE(sensor.readAlertLimitsHigh(set, clear));
      CHECK(busLog().size() == 2);  // the clear limit is not queried
    }
    SUBCASE("high, clear limit") {
      Wire.queueResponse(word(0x0133));
      Wire.queueResponse(bytes(0x01, 0x2D));  // short read
      CHECK_FALSE(sensor.readAlertLimitsHigh(set, clear));
      CHECK(busLog().size() == 4);
    }
    SUBCASE("low, set limit") {
      CHECK_FALSE(sensor.readAlertLimitsLow(set, clear));  // NACK
      CHECK(busLog().size() == 2);
    }
    SUBCASE("low, clear limit") {
      Wire.queueResponse(word(0x012D));
      Wire.queueResponse(corrupted(word(0x0133), 0));
      CHECK_FALSE(sensor.readAlertLimitsLow(set, clear));
      CHECK(busLog().size() == 4);
    }
  }
}
