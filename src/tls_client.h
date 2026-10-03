// WiFiClientSecure, without its stray close(0). Use this for every HTTPS request.
//
// arduino-esp32 2.0.17: WiFiClientSecure::stop() closes its socket and then calls
// stop_ssl_socket(), which ends by zeroing the whole client context. That leaves the
// socket number at 0 where "no socket" is -1. The next stop() - and the destructor
// always makes one - sees socket 0 as open and calls close(0): the general close, on
// file descriptor 0.
//
// The first time, that closes the console's stdin. From then on descriptor 0 is free,
// so the next file opened anywhere gets it, and the next HTTPS request to finish closes
// that file under whoever is using it. Seen from five devices as a crash in FatFs
// (f_write or f_read > validate): the map's tile task finished a download and closed
// descriptor 0 while the loop was saving the previous tile to the SD card through it.
// A save to the internal store open at that moment would have been cut short the same way.
//
// Here the socket number is put back to -1 after every stop(), so the destructor's
// stop() finds nothing to close. A socket still open when the client is destroyed is
// closed by the base class as before.
#pragma once
#include <WiFiClientSecure.h>

class TlsClient : public WiFiClientSecure {
public:
  void stop() override {
    WiFiClientSecure::stop();
    sslclient->socket = -1;
  }
};
