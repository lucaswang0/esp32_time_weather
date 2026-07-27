#include "FlipClockPage.h"
#include "config.h"
#include "TimeManager.h"
#include <math.h>

extern TimeManager timeManager;

static uint16_t _scale_buf[SCREEN_WIDTH];

static void convert_to_mono(const uint16_t* src, uint16_t* dst, int count) {
    for (int i = 0; i < count; i++) {
        dst[i] = (src[i] == 0x18C3) ? 0x0000 : 0xFFFF;
    }
}

FlipClockPage::FlipClockPage(DisplayManager& display,
                              int16_t area_x, int16_t area_y,
                              int16_t area_w, int16_t area_h)
    : _display(display), _sprite(nullptr), _sprite_ok(false),
      _area_x(area_x), _area_y(area_y),
      _area_w(area_w), _area_h(area_h),
      _vis_scale(1), _custom_scale(false), _next_render(0) {
}

FlipClockPage::~FlipClockPage() {
    if (_sprite) {
        _sprite->deleteSprite();
        delete _sprite;
    }
}

void FlipClockPage::setDisplayArea(int16_t x, int16_t y, int16_t w, int16_t h) {
    _area_x = x;
    _area_y = y;
    _area_w = w;
    _area_h = h;
}

void FlipClockPage::setScale(int16_t scale) {
    _custom_scale = true;
    _vis_scale = (scale >= 1) ? scale : 1;
}

void FlipClockPage::calc_layout() {
    int group_w = CW * 2 + GAP;
    int colon_gap = GAP + COLON_W + GAP;
    int total_w = group_w * 3 + colon_gap * 2;

    if (!_custom_scale) {
        int needed_w = total_w;
        int needed_h = CH;
        _vis_scale = 1;
        while (needed_w * (_vis_scale + 1) <= _area_w &&
               needed_h * (_vis_scale + 1) <= _area_h) {
            _vis_scale++;
        }
    }

    int scaled_w = total_w * _vis_scale;
    int scaled_h = CH * _vis_scale;

    int start_x = _area_x + (_area_w - scaled_w) / 2;
    int start_y = _area_y + (_area_h - scaled_h) / 2;

    for (int g = 0; g < 3; g++) {
        _digit_x[g * 2] = start_x + (g * (group_w + colon_gap)) * _vis_scale;
        _digit_x[g * 2 + 1] = _digit_x[g * 2] + (CW + GAP) * _vis_scale;
    }

    for (int c = 0; c < 2; c++) {
        int g = c;
        int colon_x = start_x + ((g + 1) * group_w + g * colon_gap + GAP + COLON_W / 2) * _vis_scale;
        _colon_x[c] = colon_x;
    }

    _sprite_x = _digit_x[0];
    _sprite_y = start_y;
}

void FlipClockPage::build_flip_table() {
    for (int f = 0; f < ANIM_HALF; f++) {
        float angle = (float)f / ANIM_HALF * (float)M_PI / 2;
        int vis = max(1, (int)roundf((float)HALF * cosf(angle)));
        float widen = 0.25f * sinf(angle);
        _flip_table[f].vis = (int8_t)vis;

        for (int i = 0; i < N_STRIPS; i++) {
            float t = (float)i / N_STRIPS;
            float sh = (float)vis / N_STRIPS;

            _flip_table[f].upper[i] = {
                (int8_t)roundf(-vis + i * sh),
                (int8_t)roundf(-vis + (i + 1) * sh),
                (int8_t)roundf(widen * CW * (1 - t) / 2)
            };

            _flip_table[f].lower[i] = {
                (int8_t)roundf(i * sh),
                (int8_t)roundf((i + 1) * sh),
                (int8_t)roundf(widen * CW * t / 2)
            };
        }
    }
}

void FlipClockPage::init_time() {
    int h = timeManager.getHour();
    int m = timeManager.getMinute();
    int s = timeManager.getSecond();

    _digits[0] = {h / 10, 2, h / 10, -1};
    _digits[1] = {h % 10, 9, h % 10, -1};
    _digits[2] = {m / 10, 5, m / 10, -1};
    _digits[3] = {m % 10, 9, m % 10, -1};
    _digits[4] = {s / 10, 5, s / 10, -1};
    _digits[5] = {s % 10, 9, s % 10, -1};
}

void FlipClockPage::sync_time() {
    int h = timeManager.getHour();
    int m = timeManager.getMinute();
    int s = timeManager.getSecond();

    int new_digits[6] = {
        h / 10, h % 10, m / 10, m % 10, s / 10, s % 10
    };

    for (int i = 0; i < 6; i++) {
        if (new_digits[i] != _digits[i].cur) {
            _digits[i].old = _digits[i].cur;
            _digits[i].cur = new_digits[i];
            _digits[i].anim_frame = 0;
        }
    }
}

void FlipClockPage::render_card(int idx) {
    Digit& d = _digits[idx];

    static uint16_t mono_buf[CW * HALF];
    static uint16_t mono_line[CW];

    const uint16_t* upper_src = DIGIT_UPPER[d.cur];
    const uint16_t* lower_src = DIGIT_LOWER[d.cur];
    const uint16_t* old_upper_src = DIGIT_UPPER[d.old];
    const uint16_t* old_lower_src = DIGIT_LOWER[d.old];

    if (_sprite_ok) {
        int wid_x = (_digit_x[idx] - _sprite_x) / _vis_scale;

        if (d.anim_frame < 0 || d.anim_frame >= TOTAL_FRAMES) {
            convert_to_mono(upper_src, mono_buf, CW * HALF);
            _sprite->pushImage(wid_x, 0, CW, HALF, mono_buf);
            convert_to_mono(lower_src, mono_buf, CW * HALF);
            _sprite->pushImage(wid_x, HALF, CW, HALF, mono_buf);

            if (d.anim_frame >= TOTAL_FRAMES) {
                d.anim_frame = -1;
            }
            return;
        }

        convert_to_mono(upper_src, mono_buf, CW * HALF);
        _sprite->pushImage(wid_x, 0, CW, HALF, mono_buf);
        convert_to_mono(old_lower_src, mono_buf, CW * HALF);
        _sprite->pushImage(wid_x, HALF, CW, HALF, mono_buf);

        if (d.anim_frame < ANIM_HALF) {
            int frame = d.anim_frame;
            for (int i = 0; i < N_STRIPS; i++) {
                int dy_top = HALF + _flip_table[frame].upper[i].dy_top;
                int dy_bot = HALF + _flip_table[frame].upper[i].dy_bot;
                int dst_h = dy_bot - dy_top;
                if (dst_h < 1) continue;

                int src_row = i * 2;
                if (src_row < 0) src_row = 0;
                if (src_row >= HALF) src_row = HALF - 1;

                for (int x = 0; x < CW; x++) {
                    mono_line[x] = (old_upper_src[src_row * CW + x] == 0x18C3) ? (uint16_t)0x0000 : (uint16_t)0xFFFF;
                }

                for (int y = 0; y < dst_h; y++) {
                    if (dy_top + y < 0 || dy_top + y >= CH) continue;
                    _sprite->pushImage(wid_x, dy_top + y, CW, 1, mono_line);
                }

                int ext = _flip_table[frame].upper[i].ext;
                for (int e = 0; e < ext; e++) {
                    _sprite->drawPixel(wid_x + CW + e, dy_top + dy_bot / 2, 0x0000);
                }
            }
        } else {
            int frame = TOTAL_FRAMES - 1 - d.anim_frame;
            for (int i = 0; i < N_STRIPS; i++) {
                int dy_top = HALF + _flip_table[frame].lower[i].dy_top;
                int dy_bot = HALF + _flip_table[frame].lower[i].dy_bot;
                int dst_h = dy_bot - dy_top;
                if (dst_h < 1) continue;

                int src_row = i * 2;
                if (src_row < 0) src_row = 0;
                if (src_row >= HALF) src_row = HALF - 1;

                for (int x = 0; x < CW; x++) {
                    mono_line[x] = (lower_src[src_row * CW + x] == 0x18C3) ? (uint16_t)0x0000 : (uint16_t)0xFFFF;
                }

                for (int y = 0; y < dst_h; y++) {
                    if (dy_top + y < 0 || dy_top + y >= CH) continue;
                    _sprite->pushImage(wid_x, dy_top + y, CW, 1, mono_line);
                }

                int ext = _flip_table[frame].lower[i].ext;
                for (int e = 0; e < ext; e++) {
                    _sprite->drawPixel(wid_x + CW + e, dy_top + dy_bot / 2, 0x0000);
                }
            }
        }
    } else {
        auto& tft = _display.getTFT();
        if (d.anim_frame < 0 || d.anim_frame >= TOTAL_FRAMES) {
            int scr_cx = _digit_x[idx];
            int scr_cy = _sprite_y;
            convert_to_mono(upper_src, mono_buf, CW * HALF);
            tft.pushImage(scr_cx, scr_cy, CW, HALF, mono_buf);
            convert_to_mono(lower_src, mono_buf, CW * HALF);
            tft.pushImage(scr_cx, scr_cy + HALF * _vis_scale, CW, HALF, mono_buf);
            if (d.anim_frame >= TOTAL_FRAMES) {
                d.anim_frame = -1;
            }
        }
    }
}

void FlipClockPage::render_trapezoid(int cx, const uint16_t* src, const StripEntry* strips) {
    static uint16_t mono_line[CW];
    for (int i = 0; i < N_STRIPS; i++) {
        int dy_top = HALF + strips[i].dy_top;
        int dy_bot = HALF + strips[i].dy_bot;
        int dst_h = dy_bot - dy_top;
        if (dst_h < 1) continue;

        int src_row = i * 2;
        if (src_row < 0) src_row = 0;
        if (src_row >= HALF) src_row = HALF - 1;

        for (int x = 0; x < CW; x++) {
            mono_line[x] = (src[src_row * CW + x] == 0x18C3) ? (uint16_t)0x0000 : (uint16_t)0xFFFF;
        }

        for (int y = 0; y < dst_h; y++) {
            if (dy_top + y < 0 || dy_top + y >= CH) continue;
            _sprite->pushImage(cx, dy_top + y, CW, 1, mono_line);
        }

        int ext = strips[i].ext;
        for (int e = 0; e < ext; e++) {
            _sprite->drawPixel(cx + CW + e, dy_top + dy_bot / 2, 0x0000);
        }
    }
}

void FlipClockPage::render_colon(int idx) {
    if (!_sprite_ok) return;

    int cx = (_colon_x[idx] - _sprite_x) / _vis_scale;
    int y1 = CH * 3 / 10;
    int y2 = CH * 7 / 10;
    int rad = 2;

    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            if (dx * dx + dy * dy > rad * rad) continue;
            if (y1 + dy >= 0 && y1 + dy < CH) {
                _sprite->drawPixel(cx + dx, y1 + dy, COLON_CLR);
            }
            if (y2 + dy >= 0 && y2 + dy < CH) {
                _sprite->drawPixel(cx + dx, y2 + dy, COLON_CLR);
            }
        }
    }
}

void FlipClockPage::render_frame() {
    if (!_sprite_ok) return;

    _sprite->fillSprite(BG_COLOR);

    for (int i = 0; i < 6; i++) {
        render_card(i);
    }

    for (int i = 0; i < 2; i++) {
        render_colon(i);
    }
}

void FlipClockPage::pushSpriteScaled() {
    if (!_sprite_ok) return;

    auto& tft = _display.getTFT();
    uint16_t* src = (uint16_t*)_sprite->getPointer();
    int sp_w = _sprite->width();
    int sp_h = _sprite->height();

    tft.startWrite();
    for (int y = 0; y < sp_h; y++) {
        uint16_t* sp_row = &src[y * sp_w];
        for (int x = 0; x < sp_w; x++) {
            for (int s = 0; s < _vis_scale; s++) {
                _scale_buf[x * _vis_scale + s] = sp_row[x];
            }
        }
        for (int s = 0; s < _vis_scale; s++) {
            int dst_y = _sprite_y + y * _vis_scale + s;
            if (dst_y < _area_y || dst_y >= _area_y + _area_h) continue;
            int dst_x1 = _sprite_x;
            int dst_x2 = _sprite_x + sp_w * _vis_scale - 1;
            if (dst_x1 < 0) dst_x1 = 0;
            if (dst_x2 >= SCREEN_WIDTH) dst_x2 = SCREEN_WIDTH - 1;
            tft.setWindow(dst_x1, dst_y, dst_x2, dst_y);
            int offset = (dst_x1 - _sprite_x) * _vis_scale;
            int count = (dst_x2 - dst_x1 + 1);
            tft.pushPixels(&_scale_buf[offset], count);
        }
    }
    tft.endWrite();
}

void FlipClockPage::onEnter() {
    Serial.println("[FlipClockPage] Entering flip clock page");

    calc_layout();
    Serial.printf("[FlipClockPage] area: [%d,%d,%d,%d], scale: %d\n",
                  _area_x, _area_y, _area_w, _area_h, _vis_scale);
    Serial.printf("[FlipClockPage] digit_x: [%d,%d,%d,%d,%d,%d], colon_x: [%d,%d]\n",
                  _digit_x[0], _digit_x[1], _digit_x[2], _digit_x[3], _digit_x[4], _digit_x[5],
                  _colon_x[0], _colon_x[1]);

    build_flip_table();
    init_time();

    int widget_w = (_digit_x[5] + CW * _vis_scale - _sprite_x) / _vis_scale;
    int widget_h = CH;

    Serial.printf("[FlipClockPage] sprite widget: %dx%d, heap: %d, maxBlock: %d\n",
                  widget_w, widget_h, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    auto& tft = _display.getTFT();
    tft.setSwapBytes(false);
    tft.fillScreen(BG_COLOR);

    if (!_sprite) {
        _sprite = new TFT_eSprite(&tft);
    }

    _sprite_ok = _sprite->createSprite(widget_w, widget_h);

    if (_sprite_ok) {
        _sprite->setSwapBytes(false);
        _sprite->fillSprite(BG_COLOR);
        for (int i = 0; i < 6; i++) render_card(i);
        for (int i = 0; i < 2; i++) render_colon(i);
        pushSpriteScaled();
        Serial.printf("[FlipClockPage] Sprite created, heap after: %d\n", ESP.getFreeHeap());
    } else {
        Serial.println("[FlipClockPage] Sprite create FAILED, fallback to direct draw");
        _sprite_ok = false;
        auto& tft2 = _display.getTFT();
        tft2.setSwapBytes(false);
        for (int i = 0; i < 6; i++) {
            auto& d = _digits[i];
            static uint16_t fb[CW * HALF];
            convert_to_mono(DIGIT_UPPER[d.cur], fb, CW * HALF);
            tft2.pushImage(_digit_x[i], _sprite_y, CW, HALF, fb);
            convert_to_mono(DIGIT_LOWER[d.cur], fb, CW * HALF);
            tft2.pushImage(_digit_x[i], _sprite_y + HALF * _vis_scale, CW, HALF, fb);
        }
        for (int i = 0; i < 2; i++) {
            int cx = _colon_x[i];
            int y1 = _sprite_y + CH * 3 / 10 * _vis_scale;
            int y2 = _sprite_y + CH * 7 / 10 * _vis_scale;
            for (int dy = -2 * _vis_scale; dy <= 2 * _vis_scale; dy++) {
                for (int dx = -2 * _vis_scale; dx <= 2 * _vis_scale; dx++) {
                    tft2.drawPixel(cx + dx, y1 + dy, COLON_CLR);
                    tft2.drawPixel(cx + dx, y2 + dy, COLON_CLR);
                }
            }
        }
    }

    _next_render = 0;
}

void FlipClockPage::onExit() {
    Serial.println("[FlipClockPage] Exiting flip clock page");
    if (_sprite && _sprite_ok) {
        _sprite->deleteSprite();
    }
    _sprite_ok = false;
    _display.getTFT().setSwapBytes(false);
}

void FlipClockPage::update() {
    uint32_t now = millis();

    if ((int32_t)(now - _next_render) >= 0) {
        _next_render = now + 1000 / FPS;

        static uint32_t _last_sec_check = 0;
        if (now - _last_sec_check >= 900) {
            _last_sec_check = now;
            sync_time();
        }

        if (_sprite_ok) {
            auto& tft = _display.getTFT();
            tft.setSwapBytes(false);
            render_frame();
            pushSpriteScaled();
        } else {
            auto& tft = _display.getTFT();
            static uint16_t fb[CW * HALF];
            for (int i = 0; i < 6; i++) {
                auto& d = _digits[i];
                if (d.anim_frame < 0 || d.anim_frame >= TOTAL_FRAMES) {
                    convert_to_mono(DIGIT_UPPER[d.cur], fb, CW * HALF);
                    tft.pushImage(_digit_x[i], _sprite_y, CW, HALF, fb);
                    convert_to_mono(DIGIT_LOWER[d.cur], fb, CW * HALF);
                    tft.pushImage(_digit_x[i], _sprite_y + HALF * _vis_scale, CW, HALF, fb);
                }
            }
        }

        for (int i = 0; i < 6; i++) {
            if (_digits[i].anim_frame >= 0 && _digits[i].anim_frame < TOTAL_FRAMES) {
                _digits[i].anim_frame++;
            } else if (_digits[i].anim_frame >= TOTAL_FRAMES) {
                _digits[i].anim_frame = -1;
            }
        }
    }
}

void FlipClockPage::onTouch(PageTouchType /*type*/) {
}
