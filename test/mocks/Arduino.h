// Minimal host-side fake of the Arduino core API used by the library.
#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);

#endif
