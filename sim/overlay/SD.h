#pragma once
#include "FS.h"
#include "SPI.h"
class SDFS : public fs::FS {
public:
  bool begin(uint8_t = 0, SPIClass& = SPI, uint32_t = 4000000, const char* = "/sd", uint8_t = 5, bool = false) { return false; }
  uint64_t cardSize() { return 0; }
  uint64_t totalBytes() { return 0; }
  uint64_t usedBytes() { return 0; }
  uint8_t cardType() { return 0; }
};
extern SDFS SD;
