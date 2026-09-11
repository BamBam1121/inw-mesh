#pragma once
#include <math.h>
#include "display_config.h"
#include "theme.h"
#include "statusbar.h"

struct CarouselItem {
    const char* title;
    const char* (*subtitle)();
    void        (*activate)();
};

class Carousel {
public:
    static constexpr int BAND_TOP = 24;
    static constexpr int BAND_H   = 166;
    static constexpr int PITCH    = 156;

    void begin(LGFX* d, Theme* t, const CarouselItem* items, uint8_t count) {
        _d = d; _t = t; _items = items; _count = count;
        _index = 0; _offset = 0; _dirty = true; _full = true;
        // Only the card band animates, so only it is buffered: a full-frame push
        // (~21 ms at 80 MHz) outlasts a panel refresh and tears; the band fits.
        _fb.setPsram(true);
        _fb.setColorDepth(16);
        _fb.createSprite(d->width(), BAND_H);
        // Chrome is composed off-screen too, then pushed whole: no clear-then-draw blink.
        _bar.setPsram(true); _bar.setColorDepth(16); _bar.createSprite(d->width(), BAND_TOP);
        _foot.setPsram(true); _foot.setColorDepth(16); _foot.createSprite(d->width(), d->height() - BAND_TOP - BAND_H);
    }

    // Detents move the index now and leave the offset behind to catch up, so a fast
    // flick lands on the right card and the animation is only ever cosmetic.
    void nudge(int8_t detents) {
        if (!detents || !_count) return;
        int16_t i = (int16_t)_index + detents;
        while (i < 0) i += _count;
        _index = (uint8_t)(i % _count);
        _offset -= (float)detents;
        if (_offset >  2.0f) _offset =  2.0f;     // never slide from off-stage
        if (_offset < -2.0f) _offset = -2.0f;
        _dirty = true;
    }

    void select() {
        if (_count && _items[_index].activate) _items[_index].activate();
    }

    void tick() {
        if (fabsf(_offset) < 0.01f) { if (_offset != 0) { _offset = 0; _dirty = true; } return; }
        _offset *= 0.72f;            // ease out, settles in about six frames
        _dirty = true;
    }

    bool animating() const { return fabsf(_offset) >= 0.01f; }
    bool dirty() const { return _dirty; }
    // A full repaint is only needed when arriving from another view. During a
    // slide the band is the only thing that changed, and repainting the whole
    // screen every frame would triple the SPI traffic for nothing.
    void invalidate() { _dirty = true; _full = true; }
    // Live text changed (clock, subtitles, footer): repaint it in place.
    void refresh() { _dirty = true; _chrome = true; }
    uint8_t index() const { return _index; }

    void draw() {
        if (!_dirty || !_d || !_count) return;
        const bool full = _full;
        _dirty = false; _full = false;

        // Static chrome (background + status bar) painted straight to the panel,
        // once per entry. Only the band below it animates.
        const bool chrome = _chrome;
        _chrome = false;
        if (full) { pushChrome(); }
        else if (chrome) { pushChrome(); }   // no clear: no flash
        renderBand();
        _fb.pushSprite(_d, 0, BAND_TOP);   // band only: fits inside one refresh
    }

    // Same frame composed into another surface (used while an overlay is up).
    void drawInto(lgfx::LovyanGFX& dst) {
        if (!_count) return;
        renderBand();
        _fb.pushSprite(&dst, 0, BAND_TOP);
        drawFooter(dst);
        _dirty = false;
    }

    // One line under the cards: the node's own state, live.
    const char* (*footer)() = nullptr;

private:
    void renderBand() {
        auto& d = _fb;
        d.fillScreen(_t->bg);
        d.setFont(&fonts::Font2);
        // Far cards first. Painting near to far clips the focused card's border.
        int8_t order[5] = {0}; uint8_t n = 0;
        for (int8_t k = -2; k <= 2; k++) order[n++] = k;
        for (uint8_t i = 0; i < n; i++)
            for (uint8_t j = i + 1; j < n; j++)
                if (dist(order[j]) > dist(order[i])) { const int8_t s = order[i]; order[i] = order[j]; order[j] = s; }
        for (uint8_t i = 0; i < n; i++) card(order[i]);
    }

    void pushChrome() {
        _bar.fillScreen(_t->bg);
        drawStatusBar(_bar, *_t);
        _bar.pushSprite(_d, 0, 0);
        _foot.fillScreen(_t->bg);
        if (footer) {
            _foot.setFont(&fonts::Font2);
            _foot.setTextColor(_t->dim, _t->bg);
            const char* s = footer();
            _foot.drawString(s, (_foot.width() - _foot.textWidth(s)) / 2, 8);
        }
        _foot.pushSprite(_d, 0, BAND_TOP + BAND_H);
    }

    void drawFooter(lgfx::LovyanGFX& d) {
        if (!footer) return;
        const Theme& t = *_t;
        const int y = BAND_TOP + BAND_H;
        d.fillRect(0, y, d.width(), d.height() - y, t.bg);
        d.setFont(&fonts::Font2);
        d.setTextColor(t.dim, t.bg);
        const char* s = footer();
        d.drawString(s, (d.width() - d.textWidth(s)) / 2, y + 8);
    }

    float dist(int8_t k) const { return fabsf((float)k - _offset); }

    void card(int8_t k) {
        auto& d = _fb; const Theme& t = *_t;
        const float pos = (float)k - _offset;
        const float near = fminf(fabsf(pos), 2.0f);
        const int cx = (int)lroundf(d.width() / 2.0f + pos * PITCH);
        const int w  = (int)lroundf(150 - near * 38);
        const int h  = (int)lroundf(118 - near * 26);
        if (cx + w / 2 < 0 || cx - w / 2 > d.width()) return;

        int16_t idx = (int16_t)_index + k;
        while (idx < 0) idx += _count;
        const CarouselItem& it = _items[idx % _count];

        const bool focus = (k == 0 && fabsf(_offset) < 0.25f);
        const int cy = BAND_H / 2 - 6;   // band-relative: sprite origin is BAND_TOP
        const int x0 = cx - w / 2, y0 = cy - h / 2;

        d.fillRect(x0, y0, w, h, focus ? t.panel : t.bg);
        d.drawRect(x0, y0, w, h, focus ? t.green : t.line);
        if (focus) d.drawRect(x0 - 2, y0 - 2, w + 4, h + 4, t.greenDim);

        d.setFont(focus ? &fonts::Font4 : &fonts::Font2);
        d.setTextColor(focus ? t.green : (near < 1.5f ? t.dim : t.greenDim),
                       focus ? t.panel : t.bg);
        d.drawString(it.title, cx - d.textWidth(it.title) / 2, y0 + (focus ? 12 : 10));
        d.setFont(&fonts::Font2);

        if (focus && it.subtitle) {
            const char* s = it.subtitle();
            d.setTextColor(t.txt, t.panel);
            d.drawString(s, cx - d.textWidth(s) / 2, y0 + h - 20);
        }
    }

    LGFX*  _d = nullptr;
    lgfx::LGFX_Sprite _fb, _bar, _foot;
    Theme* _t = nullptr;
    const CarouselItem* _items = nullptr;
    uint8_t _count = 0, _index = 0;
    float   _offset = 0;
    bool    _dirty = true, _full = true, _chrome = false;
};
