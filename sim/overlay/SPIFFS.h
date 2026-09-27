#pragma once
#include "FS.h"
class SPIFFSFS : public fs::FS { public: bool format() { return true; } };
extern SPIFFSFS SPIFFS;
