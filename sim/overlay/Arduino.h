// Squatch Mesh screen simulator: just enough of Arduino and ESP-IDF, on a PC, for
// the firmware's own drawing code to run unchanged. Nothing here touches hardware.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <algorithm>
#include <functional>

typedef bool boolean;
typedef uint8_t byte;

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define PULLUP 0x04
#define INPUT_PULLUP 0x05
#define PULLDOWN 0x08
#define INPUT_PULLDOWN 0x09
#define OUTPUT_OPEN_DRAIN 0x13
#define FALLING 0x02
#define RISING 0x01
#define CHANGE 0x03

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

#define IRAM_ATTR
#define DRAM_ATTR
#define PROGMEM
#define PSTR(s) (s)
#define F(s) (s)
#define pgm_read_byte(a) (*(const uint8_t*)(a))
#define pgm_read_word(a) (*(const uint16_t*)(a))
#define pgm_read_dword(a) (*(const uint32_t*)(a))
#define pgm_read_ptr(a) (*(void* const*)(a))
#define memcpy_P memcpy
#define strcpy_P strcpy
#define strlen_P strlen
#define strcmp_P strcmp
#include <cmath>
using std::isnan;
using std::isinf;

using std::min;
using std::max;
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define sq(x) ((x) * (x))
#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)

// Time runs from a clock the simulator sets (sim::setMillis), so animations can be
// frozen at a chosen moment for a screenshot.
uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield();

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int  digitalRead(uint8_t pin);
int  analogRead(uint8_t pin);
void analogWrite(uint8_t pin, int val);
uint32_t analogReadMilliVolts(uint8_t pin);
void analogReadResolution(uint8_t bits);

long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);
uint32_t esp_random();

// newlib has these; MinGW doesn't
size_t strlcpy(char* dst, const char* src, size_t size);
size_t strlcat(char* dst, const char* src, size_t size);

// ---- ESP-IDF heap -------------------------------------------------------------------
#define MALLOC_CAP_EXEC     (1 << 0)
#define MALLOC_CAP_32BIT    (1 << 1)
#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_DMA      (1 << 3)
#define MALLOC_CAP_SPIRAM   (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT  (1 << 12)
inline void* heap_caps_malloc(size_t n, uint32_t) { return malloc(n); }
inline void* heap_caps_calloc(size_t n, size_t s, uint32_t) { return calloc(n, s); }
inline void* heap_caps_realloc(void* p, size_t n, uint32_t) { return realloc(p, n); }
inline void  heap_caps_free(void* p) { free(p); }
inline size_t heap_caps_get_free_size(uint32_t) { return 4u << 20; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 1u << 20; }
inline void* ps_malloc(size_t n) { return malloc(n); }
inline void* ps_calloc(size_t n, size_t s) { return calloc(n, s); }
inline void* ps_realloc(void* p, size_t n) { return realloc(p, n); }
char* strcasestr(const char* hay, const char* needle);

struct EspClass {
  uint32_t getFreeHeap() { return 180000; }
  uint32_t getFreePsram() { return 6u << 20; }
  uint32_t getPsramSize() { return 8u << 20; }
  void restart() { exit(0); }
};
extern EspClass ESP;

// ---- FreeRTOS, as far as the screen code reaches ---------------------------------------
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define portENTER_CRITICAL_ISR(m) ((void)(m))
#define portEXIT_CRITICAL_ISR(m) ((void)(m))
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY 0xFFFFFFFF
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
typedef void* TaskHandle_t;
typedef void* SemaphoreHandle_t;
typedef void* QueueHandle_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
inline void vTaskDelay(uint32_t ms) { delay(ms); }
inline void vTaskDelete(void*) {}
inline BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, int, TaskHandle_t*, int) { return pdFALSE; }
inline BaseType_t xTaskCreate(void (*)(void*), const char*, uint32_t, void*, int, TaskHandle_t*) { return pdFALSE; }
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return (void*)1; }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return (void*)1; }
inline int xSemaphoreTake(SemaphoreHandle_t, uint32_t) { return pdTRUE; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
inline void noInterrupts() {}
inline void interrupts() {}

#include "WString.h"
#include "HardwareSerial.h"
