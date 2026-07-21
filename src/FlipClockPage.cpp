#include "FlipClockPage.h"
#include "config.h"
#include "TimeManager.h"
#include <math.h>
#include <time.h>

extern TimeManager timeManager;

FlipClockPage::FlipClockPage(DisplayManager& display) 
    : _display(display), _sprite(nullptr), _sprite_ok(false), _sprite_x(0), _sprite_y(0),
      _next_second(0), _next_render(0) {
}

FlipClockPage::~FlipClockPage() {
    if (_sprite) {
        _sprite->deleteSprite();
        delete _sprite;
    }
}

void FlipClockPage::onEnter() {
    Serial.println("[FlipClockPage] Entering flip clock page");
    
    calc_layout();
    Serial.printf("[FlipClockPage] digit_x: [%d,%d,%d,%d,%d,%d], colon_x: [%d,%d]\n",
                  digit_x[0], digit_x[1], digit_x[2], digit_x[3], digit_x[4], digit_x[5],
                  colon_x[0], colon_x[1]);
    build_flip_table();
    init_time();
    Serial.printf("[FlipClockPage] init_time digits: cur=[%d,%d,%d,%d,%d,%d]\n",
                  digits[0].cur, digits[1].cur, digits[2].cur,
                  digits[3].cur, digits[4].cur, digits[5].cur);
    
    _next_second = 0;
    _next_render = 0;
    
    int sprite_w = digit_x[0] + CW - digit_x[5];
    int sprite_h = CH;
    _sprite_x = digit_x[5];
    _sprite_y = (SCREEN_HEIGHT - CH) / 2;
    
    Serial.printf("[FlipClockPage] Sprite: %dx%d at (%d,%d), heap: %d\n", 
                  sprite_w, sprite_h, _sprite_x, _sprite_y, ESP.getFreeHeap());
    
    auto& tft = _display.getTFT();
    
    if (!_sprite) {
        _sprite = new TFT_eSprite(&tft);
    }
    
    _sprite_ok = _sprite->createSprite(sprite_w, sprite_h);
    
    if (_sprite_ok) {
        _sprite->setSwapBytes(true);
        Serial.printf("[FlipClockPage] Sprite created, heap after: %d\n", ESP.getFreeHeap());
    } else {
        Serial.println("[FlipClockPage] Sprite create FAILED, fallback to direct draw");
        tft.setSwapBytes(true);
    }
    
    tft.fillScreen(BG_COLOR);
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
    
    if (_next_second == 0) _next_second = now + 1000;
    if ((int32_t)(now - _next_second) >= 0) {
        _next_second += 1000;
        sync_time();
    }
    
    if ((int32_t)(now - _next_render) >= 0) {
        _next_render = now + 1000 / FPS;
        
        for (int i = 0; i < 6; i++) {
            if (digits[i].anim_frame >= 0 && digits[i].anim_frame < TOTAL_FRAMES) {
                digits[i].anim_frame++;
            } else if (digits[i].anim_frame >= TOTAL_FRAMES) {
                digits[i].anim_frame = -1;
            }
        }
        
        if (_sprite_ok) {
            render_frame();
            _sprite->pushSprite(_sprite_x, _sprite_y);
        } else {
            auto& tft = _display.getTFT();
            tft.fillScreen(BG_COLOR);
            for (int i = 0; i < 6; i++) {
                render_card(i);
            }
            for (int i = 0; i < 2; i++) {
                render_colon(i);
            }
        }
    }
}

void FlipClockPage::onTouch(PageTouchType type) {
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

void FlipClockPage::calc_layout() {
    int group_w = CW * 2 + GAP;
    int colon_gap = GAP + COLON_W + GAP;
    int total_w = group_w * 3 + colon_gap * 2;
    
    int x;
    if (total_w <= SCREEN_WIDTH) {
        x = (SCREEN_WIDTH - total_w) / 2;
    } else {
        x = 0;
    }
    
    for (int g = 0; g < 3; g++) {
        digit_x[5 - g * 2] = x;
        digit_x[4 - g * 2] = x + CW + GAP;
        x += group_w;
        if (g < 2) {
            colon_x[g] = x + GAP + COLON_W / 2;
            x += colon_gap;
        }
    }
}

void FlipClockPage::build_flip_table() {
    for (int f = 0; f < ANIM_HALF; f++) {
        float angle = (float)f / ANIM_HALF * (float)M_PI / 2;
        int vis = max(1, (int)roundf((float)HALF * cosf(angle)));
        float widen = 0.25f * sinf(angle);
        flip_table[f].vis = (int8_t)vis;
        
        for (int i = 0; i < N_STRIPS; i++) {
            float t = (float)i / N_STRIPS;
            float sh = (float)vis / N_STRIPS;
            
            flip_table[f].upper[i] = {
                (int8_t)roundf(-vis + i * sh),
                (int8_t)roundf(-vis + (i + 1) * sh),
                (int8_t)roundf(widen * CW * (1 - t) / 2)
            };
            
            flip_table[f].lower[i] = {
                (int8_t)roundf(i * sh),
                (int8_t)roundf((i + 1) * sh),
                (int8_t)roundf(widen * CW * t / 2)
            };
        }
    }
}

void FlipClockPage::init_time() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        int h = timeinfo.tm_hour;
        int m = timeinfo.tm_min;
        int s = timeinfo.tm_sec;
        
        digits[0] = {s % 10, 9, s % 10, -1};
        digits[1] = {s / 10, 5, s / 10, -1};
        digits[2] = {m % 10, 9, m % 10, -1};
        digits[3] = {m / 10, 5, m / 10, -1};
        digits[4] = {h % 10, 9, h % 10, -1};
        digits[5] = {h / 10, 2, h / 10, -1};
    } else {
        for (int i = 0; i < 6; i++) digits[i] = {0, 9, 0, -1};
    }
}

void FlipClockPage::sync_time() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) return;
    
    int h = timeinfo.tm_hour;
    int m = timeinfo.tm_min;
    int s = timeinfo.tm_sec;
    
    int new_digits[6] = {
        s % 10,
        s / 10,
        m % 10,
        m / 10,
        h % 10,
        h / 10
    };
    
    for (int i = 0; i < 6; i++) {
        if (new_digits[i] != digits[i].cur) {
            digits[i].old = digits[i].cur;
            digits[i].cur = new_digits[i];
            digits[i].anim_frame = 0;
        }
    }
}

void FlipClockPage::render_card(int idx) {
    Digit &d = digits[idx];
    int cx = _sprite_ok ? (digit_x[idx] - _sprite_x) : digit_x[idx];
    int scr_cx = digit_x[idx];
    int scr_cy = (SCREEN_HEIGHT - CH) / 2;
    
    if (d.anim_frame < 0 || d.anim_frame >= TOTAL_FRAMES) {
        if (_sprite_ok) {
            _sprite->pushImage(cx, 0, CW, HALF, (uint16_t*)DIGIT_UPPER[d.cur]);
            _sprite->pushImage(cx, HALF, CW, HALF, (uint16_t*)DIGIT_LOWER[d.cur]);
        } else {
            auto& tft = _display.getTFT();
            tft.pushImage(scr_cx, scr_cy, CW, HALF, (uint16_t*)DIGIT_UPPER[d.cur]);
            tft.pushImage(scr_cx, scr_cy + HALF, CW, HALF, (uint16_t*)DIGIT_LOWER[d.cur]);
        }
        
        if (d.anim_frame >= TOTAL_FRAMES) {
            d.anim_frame = -1;
        }
        return;
    }
    
    if (_sprite_ok) {
        _sprite->pushImage(cx, 0, CW, HALF, (uint16_t*)DIGIT_UPPER[d.cur]);
        _sprite->pushImage(cx, HALF, CW, HALF, (uint16_t*)DIGIT_LOWER[d.old]);
        
        if (d.anim_frame < ANIM_HALF) {
            render_trapezoid(cx, DIGIT_UPPER[d.old], flip_table[d.anim_frame].upper);
        } else {
            render_trapezoid(cx, DIGIT_LOWER[d.cur], flip_table[TOTAL_FRAMES - 1 - d.anim_frame].lower);
        }
    } else {
        auto& tft = _display.getTFT();
        tft.pushImage(scr_cx, scr_cy, CW, HALF, (uint16_t*)DIGIT_UPPER[d.cur]);
        tft.pushImage(scr_cx, scr_cy + HALF, CW, HALF, (uint16_t*)DIGIT_LOWER[d.old]);
    }
}

void FlipClockPage::render_trapezoid(int cx, const uint16_t *src, const StripEntry *strips) {
    if (!_sprite_ok) return;
    
    for (int i = 0; i < N_STRIPS; i++) {
        int dy_top = HALF + strips[i].dy_top;
        int dy_bot = HALF + strips[i].dy_bot;
        int dst_h = dy_bot - dy_top;
        
        if (dst_h < 1) continue;
        
        int mid = dst_h >> 1;
        int ext = strips[i].ext;
        
        for (int y = 0; y < dst_h; y++) {
            int dst_row = dy_top + y;
            
            if (dst_row < 0 || dst_row >= CH) continue;
            
            int src_row = 2 * i + ((y < mid) ? 0 : 1);
            uint16_t line[CW];
            for (int x = 0; x < CW; x++) {
                line[x] = src[src_row * CW + x];
            }
            _sprite->pushImage(cx, dst_row, CW, 1, line);
            
            for (int e = 0; e < ext; e++) {
                if (cx + CW + e < _sprite->width()) {
                    _sprite->drawPixel(cx + CW + e, dst_row, BG_COLOR);
                }
            }
        }
    }
}

void FlipClockPage::render_colon(int idx) {
    int cx = _sprite_ok ? (colon_x[idx] - _sprite_x) : colon_x[idx];
    int scr_cx = colon_x[idx];
    int scr_cy = (SCREEN_HEIGHT - CH) / 2;
    int y1 = CH * 3 / 10;
    int y2 = CH * 7 / 10;
    
    if (_sprite_ok) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx * dx + dy * dy > 5) continue;
                
                if (y1 + dy >= 0 && y1 + dy < CH) {
                    _sprite->drawPixel(cx + dx, y1 + dy, COLON_CLR);
                }
                if (y2 + dy >= 0 && y2 + dy < CH) {
                    _sprite->drawPixel(cx + dx, y2 + dy, COLON_CLR);
                }
            }
        }
    } else {
        auto& tft = _display.getTFT();
        
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx * dx + dy * dy > 5) continue;
                
                if (scr_cy + y1 + dy >= 0 && scr_cy + y1 + dy < SCREEN_HEIGHT) {
                    tft.drawPixel(scr_cx + dx, scr_cy + y1 + dy, COLON_CLR);
                }
                if (scr_cy + y2 + dy >= 0 && scr_cy + y2 + dy < SCREEN_HEIGHT) {
                    tft.drawPixel(scr_cx + dx, scr_cy + y2 + dy, COLON_CLR);
                }
            }
        }
    }
}
