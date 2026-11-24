Sensirion STS3x Arduino Library
===================
[![CI](https://github.com/PatrickBaus/Arduino-STS3x/actions/workflows/ci.yml/badge.svg)](https://github.com/PatrickBaus/Arduino-STS3x/actions/workflows/ci.yml)

This repository contains an Arduino library for the following Sensirion devices:
 * [STS30A-DIS](https://sensirion.com/products/catalog/STS30A-DIS/)
 * [STS30-DIS](https://sensirion.com/products/catalog/STS30-DIS/)
 * [STS31A-DIS](https://sensirion.com/products/catalog/STS31A-DIS/)
 * [STS31-DIS](https://sensirion.com/products/catalog/STS31-DIS/)
 * [STS32-DIS](https://sensirion.com/products/catalog/STS32-DIS/)
 * [STS33-DIS](https://sensirion.com/products/catalog/STS33-DIS/)
 * [STS35-DIS](https://sensirion.com/products/catalog/STS35-DIS/)

The library uses I²C transactions and allows to select the I²C controller if there is more than one.

Usage
-----
```cpp
#include <Wire.h>   // #include <i2c_t3.h> on the Teensy platform.
#include "src/STS3x.h"

STS3x sensor(Wire); // To use a non-default address use STS3x sensor(Wire, address);

void setup() {
  Wire.begin();
  Serial.begin(115200);
}

void loop() {
  // The call to readTemp() will automatically block for the time the measurement takes to complete
  Serial.print(sensor.readTemp());  // Read the temperarture as a float in °C
  Serial.println(" °C");
  delay(2000);
}
```

Installation
-----
Currently the library does not support the Arduino library manager, so it is highly recommended to copy the full library to a subfolder called
```
src/
```
within your Arduino project.

Tests
-----
The library comes with unit tests that run on a (Linux) host without any hardware. They compile the library against a small fake of the Arduino `Wire` API (see `test/mocks/`), which records all bus traffic and plays back scripted sensor answers. [CMake](https://cmake.org/) (>= 3.14), a C++11 compiler and, for the coverage report, [gcovr](https://gcovr.com/) are required. [doctest](https://github.com/doctest/doctest) is downloaded by CMake.
```bash
cmake -S test -B build
cmake --build build
ctest --test-dir build --output-on-failure

# Coverage of src/, fails below 100 % line or branch coverage
gcovr --root . --filter src/ --exclude-throw-branches --fail-under-line 100 --fail-under-branch 100 --print-summary build
```
