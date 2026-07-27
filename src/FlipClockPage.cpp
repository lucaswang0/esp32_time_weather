#include "FlipClockPage.h"
#include "config.h"
#include "TimeManager.h"
#include <math.h>
#include <time.h>

extern TimeManager timeManager;

static uint16_t _scale_buf[SCREEN_WIDTH];
static uint16_t _mono_buf[17 * 14];  // CW * HALF

static void convert_to_mono(const uint16_t* src, uint16_t* dst, int count) {
    for (int i = 0; i < count; i++) {
        dst[i] = (src[i] == 0x18C3) ? 0x0000 : 0xFFFF;
    }
}

FlipClockPage::FlipClockPage(DisplayManager& display)
    : _display(display), _sprite(nullptr), _sprite_ok(false), _sprite_x(0), _sprite_y(0),
      _next_render(0), _vis_scale(2) {
}

FlipClockPage::~FlipClockPage() {
    if (_sprite) {
        _sprite->deleteSprite();
        delete _sprite;
    }
}

void FlipClockPage::calc_layout() {
    int group_w = CW * 2 + GAP;
    int colon_gap = GAP + COLON_W + GAP;
    int total_w = group_w * 3 + colon_gap * 2;

    int start_x = (SCREEN_WIDTH - total_w * _vis_scale) / 2;
    int start_y = (SCREEN_HEIGHT - CH * _vis_scale) / 2;

    _digit_x[0] = start_x;
    _digit_x[1] = start_x + (CW + GAP) * _vis_scale;

    int cx = start_x + group_w * _vis_scale;
    _colon_x[0] = cx + (GAP + COLON_W / 2) * _vis_scale;

    cx += colon_gap * _vis_scale;
    _digit_x[2] = cx;
    _digit_x[3] = cx + (CW + GAP) * _vis_scale;

    cx += group_w * _vis_scale;
    _colon_x[1] = cx + (GAP + COLON_W / 2) * _vis_scale;

    cx += colon_gap * _vis_scale;
    _digit_x[4] = cx;
    _digit_x[5] = cx + (CW + GAP) * _vis_scale;

    _sprite_x = start_x;
    _sprite_y = start_y;
}

void FlipClockPage::build_flip_table() {
    for (int f = 0; f < ANIM_HALF; f++) {
        float angle = (float)f / ANIM_HALF * (float)M_PI / 2;
        int vis_val = (int)roundf((float)HALF * cosf(angle));
        int vis = (vis_val > 1) ? vis_val : 1;
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

void FlipClockPage::update_time() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) return;

    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;

    int new_digits[6] = {h / 10, h % 10, m / 10, m % 10, s / 10, s % 10};

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

    const uint16_t* upper_src = DIGIT_UPPER[d.cur];
    const uint16_t* lower_src = DIGIT_LOWER[d.cur];
    const uint16_t* old_upper_src = DIGIT_UPPER[d.old];
    const uint16_t* old_lower_src = DIGIT_LOWER[d.old];
    static uint16_t mono_line[CW];

    if (_sprite_ok) {
        int wid_x = (_digit_x[idx] - _sprite_x) / _vis_scale;

        convert_to_mono(upper_src, _mono_buf, CW * HALF);
        _sprite->pushImage(wid_x, 0, CW, HALF, _mono_buf);

        convert_to_mono(lower_src, _mono_buf, CW * HALF);
        _sprite->pushImage(wid_x, HALF, CW, HALF, _mono_buf);

        if (d.anim_frame < 0 || d.anim_frame >= TOTAL_FRAMES) {
            if (d.anim_frame >= TOTAL_FRAMES) {
                d.anim_frame = -1;
            }
            return;
        }

        convert_to_mono(old_lower_src, _mono_buf, CW * HALF);
        _sprite->pushImage(wid_x, HALF, CW, HALF, _mono_buf);

        if (d.anim_frame < ANIM_HALF) {
            for (int i = 0; i < N_STRIPS; i++) {
                int dy_top = HALF + _flip_table[d.anim_frame].upper[i].dy_top;
                int dy_bot = HALF + _flip_table[d.anim_frame].upper[i].dy_bot;
                int dst_h = dy_bot - dy_top;
                if (dst_h < 1) continue;

                int src_row = i;
                if (src_row < 0) src_row = 0;
                if (src_row >= HALF) src_row = HALF - 1;

                for (int x = 0; x < CW; x++) {
                    mono_line[x] = (old_upper_src[src_row * CW + x] == 0x18C3) ? (uint16_t)0x0000 : (uint16_t)0xFFFF;
                }

                for (int y = 0; y < dst_h; y++) {
                    if (dy_top + y < 0 || dy_top + y >= CH) continue;
                    _sprite->pushImage(wid_x, dy_top + y, CW, 1, mono_line);
                }
            }
        } else {
            int frame = TOTAL_FRAMES - 1 - d.anim_frame;
            for (int i = 0; i < N_STRIPS; i++) {
                int dy_top = HALF + _flip_table[frame].lower[i].dy_top;
                int dy_bot = HALF + _flip_table[frame].lower[i].dy_bot;
                int dst_h = dy_bot - dy_top;
                if (dst_h < 1) continue;

                int src_row = i;
                if (src_row < 0) src_row = 0;
                if (src_row >= HALF) src_row = HALF - 1;

                for (int x = 0; x < CW; x++) {
                    mono_line[x] = (lower_src[src_row * CW + x] == 0x18C3) ? (uint16_t)0x0000 : (uint16_t)0xFFFF;
                }

                for (int y = 0; y < dst_h; y++) {
                    if (dy_top + y < 0 || dy_top + y >= CH) continue;
                    _sprite->pushImage(wid_x, dy_top + y, CW, 1, mono_line);
                }
            }
        }
    } else {
        auto& tft = _display.getTFT();
        if (d.anim_frame < 0 || d.anim_frame >= TOTAL_FRAMES) {
            convert_to_mono(upper_src, _mono_buf, CW * HALF);
            tft.pushImage(_digit_x[idx], _sprite_y, CW, HALF, _mono_buf);
            convert_to_mono(lower_src, _mono_buf, CW * HALF);
            tft.pushImage(_digit_x[idx], _sprite_y + HALF * _vis_scale, CW, HALF, _mono_buf);
            if (d.anim_frame >= TOTAL_FRAMES) {
                d.anim_frame = -1;
            }
        }
    }
}

void FlipClockPage::render_colon(int idx) {
    if (!_sprite_ok) return;

    int cx = (_colon_x[idx] - _sprite_x) / _vis_scale;
    int y1 = CH * 3 / 10;
    int y2 = CH * 7 / 10;

    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
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
    int sp_w = _sprite->width();   // widget width (total widget pixels)
    int sp_h = _sprite->height();  // widget height (CH=28)
    int out_w = sp_w * _vis_scale;
    int out_h = sp_h * _vis_scale;

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
            if (dst_y < 0 || dst_y >= SCREEN_HEIGHT) continue;
            int dst_x1 = _sprite_x;
            int dst_x2 = _sprite_x + out_w - 1;
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
    Serial.printf("[FlipClockPage] digit_x: [%d,%d,%d,%d,%d,%d], colon_x: [%d,%d]\n",
                  _digit_x[0], _digit_x[1], _digit_x[2], _digit_x[3], _digit_x[4], _digit_x[5],
                  _colon_x[0], _colon_x[1]);

    build_flip_table();

    struct tm timeinfo;
    int h = 0, m = 0, s = 0;
    if (getLocalTime(&timeinfo, 0)) {
        h = timeinfo.tm_hour;
        m = timeinfo.tm_min;
        s = timeinfo.tm_sec;
    }

    _digits[0] = {(uint8_t)(h / 10), (uint8_t)2, (uint8_t)(h / 10), (int8_t)-1};
    _digits[1] = {(uint8_t)(h % 10), (uint8_t)9, (uint8_t)(h % 10), (int8_t)-1};
    _digits[2] = {(uint8_t)(m / 10), (uint8_t)5, (uint8_t)(m / 10), (int8_t)-1};
    _digits[3] = {(uint8_t)(m % 10), (uint8_t)9, (uint8_t)(m % 10), (int8_t)-1};
    _digits[4] = {(uint8_t)(s / 10), (uint8_t)5, (uint8_t)(s / 10), (int8_t)-1};
    _digits[5] = {(uint8_t)(s % 10), (uint8_t)9, (uint8_t)(s % 10), (int8_t)-1};

    Serial.printf("[FlipClockPage] init digits: cur=[%d,%d,%d,%d,%d,%d]\n",
                  _digits[0].cur, _digits[1].cur, _digits[2].cur,
                  _digits[3].cur, _digits[4].cur, _digits[5].cur);

    int sprite_w = _digit_x[5] + CW * _vis_scale - _sprite_x;
    int sprite_h = CH * _vis_scale;

    Serial.printf("[FlipClockPage] target sprite: %dx%d (widget res), heap: %d, maxBlock: %d\n",
                  sprite_w / _vis_scale, sprite_h / _vis_scale,
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    auto& tft = _display.getTFT();
    tft.setSwapBytes(false);
    tft.fillScreen(BG_COLOR);

    if (!_sprite) {
        _sprite = new TFT_eSprite(&tft);
    }

    int widget_w = sprite_w / _vis_scale;
    int widget_h = sprite_h / _vis_scale;
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
        tft.setSwapBytes(false);
        tft.fillScreen(BG_COLOR);
        for (int i = 0; i < 6; i++) {
            auto& d = _digits[i];
            tft.pushImage(_digit_x[i], _sprite_y, CW, HALF, DIGIT_UPPER[d.cur]);
            tft.pushImage(_digit_x[i], _sprite_y + HALF * _vis_scale, CW, HALF, DIGIT_LOWER[d.cur]);
        }
        for (int i = 0; i < 2; i++) {
            int cx = _colon_x[i];
            int y1 = _sprite_y + CH * 3 / 10 * _vis_scale;
            int y2 = _sprite_y + CH * 7 / 10 * _vis_scale;
            for (int dy = -_vis_scale; dy <= _vis_scale; dy++) {
                for (int dx = -_vis_scale; dx <= _vis_scale; dx++) {
                    if (cx + dx >= 0 && cx < SCREEN_WIDTH) {
                        tft.drawPixel(cx + dx, y1 + dy, COLON_CLR);
                        tft.drawPixel(cx + dx, y2 + dy, COLON_CLR);
                    }
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

        update_time();

        if (_sprite_ok) {
            auto& tft = _display.getTFT();
            tft.setSwapBytes(false);
            render_frame();
            pushSpriteScaled();
        } else {
            auto& tft = _display.getTFT();
            for (int i = 0; i < 6; i++) {
                auto& d = _digits[i];
                if (d.anim_frame < 0 || d.anim_frame >= TOTAL_FRAMES) {
                    tft.pushImage(_digit_x[i], _sprite_y, CW, HALF, DIGIT_UPPER[d.cur]);
                    tft.pushImage(_digit_x[i], _sprite_y + HALF * _vis_scale, CW, HALF, DIGIT_LOWER[d.cur]);
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
