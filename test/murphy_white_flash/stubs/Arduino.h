#pragma once
#include <cstdint>
#define PROGMEM
#define HIGH 1
#define LOW 0
inline void delay(unsigned long) {}
inline unsigned long millis() { return 0; }
inline int digitalRead(int) { return LOW; }
inline uint8_t pgm_read_byte(const unsigned char* p) { return *p; }
