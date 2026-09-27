// SPI for the simulator: a bus with nothing on it.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
#define MSBFIRST 1
#define LSBFIRST 0
#define FSPI 0
#define HSPI 1

struct SPISettings {
  SPISettings(uint32_t = 1000000, uint8_t = MSBFIRST, uint8_t = SPI_MODE0) {}
};

class SPIClass {
public:
  SPIClass(int = 0) {}
  void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1) {}
  void end() {}
  void beginTransaction(SPISettings) {}
  void endTransaction() {}
  uint8_t transfer(uint8_t) { return 0; }
  void transfer(void*, size_t) {}
  uint16_t transfer16(uint16_t) { return 0; }
  void writeBytes(const uint8_t*, size_t) {}
  void setFrequency(uint32_t) {}
};
extern SPIClass SPI;
