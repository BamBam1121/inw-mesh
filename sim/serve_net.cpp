// A tiny HTTP server for the live simulator ("squatch_sim OUT serve PORT"), on its
// own so Windows' headers stay out of the firmware's code. Localhost only: reach it
// over an SSH tunnel or `tailscale serve`, never an open port.
#include <winsock2.h>
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include "serve_net.h"

static SOCKET s_listen = INVALID_SOCKET;

uint32_t net_ms() { return GetTickCount(); }

bool net_listen(int port) {
  WSADATA w;
  if (WSAStartup(MAKEWORD(2, 2), &w)) return false;
  s_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s_listen == INVALID_SOCKET) return false;
  BOOL yes = TRUE;
  setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons((u_short)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(s_listen, (sockaddr*)&a, sizeof(a)) || listen(s_listen, 16)) return false;
  return true;
}

int net_poll(int timeoutMs, char* req, int cap) {
  fd_set r;
  FD_ZERO(&r);
  FD_SET(s_listen, &r);
  timeval tv = {timeoutMs / 1000, (timeoutMs % 1000) * 1000};
  if (select(0, &r, nullptr, nullptr, &tv) <= 0) return -1;
  SOCKET c = accept(s_listen, nullptr, nullptr);
  if (c == INVALID_SOCKET) return -1;
  DWORD to = 2000;
  setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
  int n = 0;
  while (n < cap - 1) {                       // just the request line and headers
    const int k = recv(c, req + n, cap - 1 - n, 0);
    if (k <= 0) break;
    n += k;
    req[n] = 0;
    if (strstr(req, "\r\n\r\n")) break;
  }
  req[n] = 0;
  if (n <= 0) { closesocket(c); return -1; }
  return (int)c;
}

void net_reply(int fd, int code, const char* type, const void* body, int len) {
  SOCKET c = (SOCKET)fd;
  char head[256];
  const int h = snprintf(head, sizeof(head),
                         "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
                         "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                         code, code == 200 ? "OK" : code == 204 ? "No Content" : "Not Found", type, len);
  send(c, head, h, 0);
  const char* p = (const char*)body;
  while (len > 0) {
    const int k = send(c, p, len, 0);
    if (k <= 0) break;
    p += k;
    len -= k;
  }
  shutdown(c, SD_SEND);
  closesocket(c);
}
