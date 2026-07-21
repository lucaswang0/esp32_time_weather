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

void FlipClockPage::push_scaled(int dst_x, int dst_y, int src_w, int src_h, int dst_w, int dst_h, const uint16_t *src, int src_pitch) {
    if (!_sprite_ok) return;
    
    int sp_w = _sprite->width();
    uint16_t* buf = (uint16_t*)_sprite->getPointer();
    int max_x = dst_w - 1;
    int max_y = dst_h - 1;
    
    for (int sy = 0; sy < src_h; sy++) {
        int dy0 = dst_y + sy * max_y / (src_h - 1);
        int dy1 = dst_y + (sy + 1) * max_y / (src_h - 1);
        if (dy0 == dy1) dy1 = dy0 + 1;
        
        for (int sx = 0; sx < src_w; sx++) {
            uint16_t pixel = (src[sy * src_pitch + sx] >> 8) | (src[sy * src_pitch + sx] << 8);
            
            int dx0 = dst_x + sx * max_x / (src_w - 1);
            int dx1 = dst_x + (sx + 1) * max_x / (src_w - 1);
            if (dx0 == dx1) dx1 = dx0 + 1;
            
            for (int dy = dy0; dy < dy1; dy++) {
                if (dy < 0 || dy >= CH) continue;
                for (int dx = dx0; dx < dx1; dx++) {
                    if (dx >= 0 && dx < sp_w) {
                        buf[dy * sp_w + dx] = pixel;
                    }
                }
            }
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
            push_scaled(cx, 0,     SRC_W, SRC_HALF, CW, HALF, (uint16_t*)DIGIT_UPPER[d.cur], SRC_W);
            push_scaled(cx, HALF,   SRC_W, SRC_HALF, CW, HALF, (uint16_t*)DIGIT_LOWER[d.cur], SRC_W);
        } else {
            auto& tft = _display.getTFT();
            tft.pushImage(scr_cx, scr_cy, SRC_W, SRC_HALF, (uint16_t*)DIGIT_UPPER[d.cur]);
            tft.pushImage(scr_cx, scr_cy + SRC_HALF, SRC_W, SRC_HALF, (uint16_t*)DIGIT_LOWER[d.cur]);
        }
        
        if (d.anim_frame >= TOTAL_FRAMES) {
            d.anim_frame = -1;
        }
        return;
    }
    
    if (_sprite_ok) {
        push_scaled(cx, 0,     SRC_W, SRC_HALF, CW, HALF, (uint16_t*)DIGIT_UPPER[d.cur], SRC_W);
        push_scaled(cx, HALF,   SRC_W, SRC_HALF, CW, HALF, (uint16_t*)DIGIT_LOWER[d.old], SRC_W);
        
        if (d.anim_frame < ANIM_HALF) {
            render_trapezoid(cx, DIGIT_UPPER[d.old], flip_table[d.anim_frame].upper);
        } else {
            render_trapezoid(cx, DIGIT_LOWER[d.cur], flip_table[TOTAL_FRAMES - 1 - d.anim_frame].lower);
        }
    } else {
        auto& tft = _display.getTFT();
        tft.pushImage(scr_cx, scr_cy, SRC_W, SRC_HALF, (uint16_t*)DIGIT_UPPER[d.cur]);
        tft.pushImage(scr_cx, scr_cy + SRC_HALF, SRC_W, SRC_HALF, (uint16_t*)DIGIT_LOWER[d.old]);
    }
}

void FlipClockPage::render_trapezoid(int cx, const uint16_t *src, const StripEntry *strips) {
    if (!_sprite_ok) return;
    
    int sp_w = _sprite->width();
    uint16_t* buf = (uint16_t*)_sprite->getPointer();
    int max_x = CW - 1;
    
    for (int i = 0; i < N_STRIPS; i++) {
        int dy_top = HALF + strips[i].dy_top;
        int dy_bot = HALF + strips[i].dy_bot;
        int dst_h = dy_bot - dy_top;
        
        if (dst_h < 1) {
            dy_bot = dy_top + 1;
            dst_h = 1;
        }
        
        int mid = dst_h >> 1;
        int ext = strips[i].ext;
        
        for (int y = 0; y < dst_h; y++) {
            int dst_row = dy_top + y;
            
            if (dst_row < 0 || dst_row >= CH) continue;
            
            int src_row = 2 * i + ((y < mid) ? 0 : 1);
            
            for (int x = 0; x < SRC_W; x++) {
                uint16_t raw = src[src_row * SRC_W + x];
                uint16_t pixel = (raw >> 8) | (raw << 8);
                int dx0 = cx + x * max_x / (SRC_W - 1);
                int dx1 = cx + (x + 1) * max_x / (SRC_W - 1);
                if (dx0 == dx1) dx1 = dx0 + 1;
                
                int dy0 = dst_row;
                int dy1 = dst_row + 1;
                if (dy0 == dy1) dy1 = dy0 + 1;
                
                for (int dy = dy0; dy < dy1; dy++) {
                    if (dy < 0 || dy >= CH) continue;
                    for (int dx = dx0; dx < dx1; dx++) {
                        if (dx >= 0 && dx < sp_w) {
                            buf[dy * sp_w + dx] = pixel;
                        }
                    }
                }
            }
            
            int ext_dx_start = cx + CW;
            int ext_dx_end = ext_dx_start + (ext * (CW - 1)) / (SRC_W - 1);
            for (int dx = ext_dx_start; dx < ext_dx_end && dx < sp_w; dx++) {
                int dy0 = dst_row;
                int dy1 = dst_row + 1;
                if (dy0 == dy1) dy1 = dy0 + 1;
                for (int dy = dy0; dy < dy1; dy++) {
                    if (dy >= 0 && dy < CH) {
                        buf[dy * sp_w + dx] = (BG_COLOR >> 8) | (BG_COLOR << 8);
                    }
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
    int r = 3;
    
    if (_sprite_ok) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx * dx + dy * dy > r * r + 1) continue;
                
                int px = cx + dx * (CW - 1) / (SRC_W - 1);
                int py1 = y1 + dy * (CH - 1) / (HALF - 1);
                int py2 = y2 + dy * (CH - 1) / (HALF - 1);
                
                for (int oy = -1; oy <= 1; oy++) {
                    for (int ox = -1; ox <= 1; ox++) {
                        int fx = px + ox;
                        int fy1 = py1 + oy;
                        int fy2 = py2 + oy;
                        if (fx >= 0 && fx < _sprite->width()) {
                            if (fy1 >= 0 && fy1 < CH) _sprite->drawPixel(fx, fy1, COLON_CLR);
                            if (fy2 >= 0 && fy2 < CH) _sprite->drawPixel(fx, fy2, COLON_CLR);
                        }
                    }
                }
            }
        }
    } else {
        auto& tft = _display.getTFT();
        
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (dx * dx + dy * dy > r * r + 1) continue;
                
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
