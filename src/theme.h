// Colours, resolved once at boot.

#pragma once
#include "display_config.h"

struct Theme {
    uint16_t bg, panel, line, green, greenDim, txt, dim, amber;
    uint16_t red, white, bubbleIn, bubbleOut, blue, focus;

    void init(LGFX& d) {
        bg        = d.color565(0x06, 0x0a, 0x09);
        panel     = d.color565(0x0b, 0x12, 0x0e);
        line      = d.color565(0x16, 0x24, 0x1c);
        green     = d.color565(0x3d, 0xff, 0xa8);
        greenDim  = d.color565(0x1f, 0x6f, 0x4e);
        txt       = d.color565(0xb9, 0xd4, 0xc6);
        dim       = d.color565(0x5f, 0x80, 0x74);
        amber     = d.color565(0xe6, 0xb9, 0x55);
        red       = d.color565(0xff, 0x5a, 0x5a);
        white     = d.color565(0xe8, 0xf2, 0xed);
        bubbleIn  = d.color565(0x16, 0x1d, 0x1a);
        bubbleOut = d.color565(0x0f, 0x3d, 0x33);
        blue      = d.color565(0x4d, 0xa3, 0xff);
        focus     = d.color565(0x12, 0x2a, 0x21);
    }
};
