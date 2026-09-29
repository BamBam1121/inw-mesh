// The live simulator's HTTP server (serve_net.cpp): localhost only.
#pragma once
#include <stdint.h>

uint32_t net_ms();                                  // real milliseconds
bool net_listen(int port);                          // 127.0.0.1:port
// Waits up to timeoutMs for a request. Returns its connection (answer it with
// net_reply) with the request line and headers in req, or -1.
int net_poll(int timeoutMs, char* req, int cap);
void net_reply(int fd, int code, const char* type, const void* body, int len);
