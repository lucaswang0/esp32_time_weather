#include "FlipClockPage.h"
#include "config.h"
#include <math.h>

FlipClockPage::FlipClockPage(DisplayManager& display) : _display(display) {
}

void FlipClockPage::onEnter() {
    Serial.println("[FlipClockPage] Entering flip clock page");
    
    calc_layout();
    build_flip_table();
    init_time();
    
    _next_second = 0;
    _next_render = 0;
}

void FlipClockPage::onExit() {
    Serial.println("[FlipClockPage] Exiting flip clock page");
}

void FlipClockPage::update() {
    uint32_t now = millis();
    
    if (_next_second == 0) _next_second = now + 1000;
    if ((int32_t)(now - _next_second) >= 0) {
        _next_second += 1000;
        advance(0);
    }
    
    if ((int32_t)(now - _next_render) >= 0) {
        _next_render = now + 1000 / FPS;
        
        _display.getTFT().fillScreen(BG_COLOR);
        
        for (int i = 0; i < 6; i++) {
            render_card(i);
        }
        
        for (int i = 0; i < 2; i++) {
            render_colon(colon_x[i]);
        }
        
        for (int i = 0; i < 6; i++) {
            if (digits[i].anim_frame >= 0) {
                digits[i].anim_frame++;
            }
        }
    }
}

void FlipClockPage::onTouch(PageTouchType type) {
}

void FlipClockPage::calc_layout() {
    int group_w = CW * 2 + GAP;
    int colon_gap = GAP + COLON_W + GAP;
    int total_w = group_w * 3 + colon_gap * 2;
    int x = (SCREEN_WIDTH - total_w) / 2;
    
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
    digits[0] = {0, 9, 0, -1};
    digits[1] = {0, 5, 0, -1};
    digits[2] = {0, 9, 0, -1};
    digits[3] = {0, 5, 0, -1};
    digits[4] = {2, 9, 2, -1};
    digits[5] = {1, 2, 1, -1};
}

void FlipClockPage::advance(int idx) {
    if (idx >= 6) return;
    
    Digit &d = digits[idx];
    d.old = d.cur;
    d.cur++;
    
    bool overflow = false;
    
    if (idx == 4) {
        int h_max = (digits[5].cur == 2) ? 3 : 9;
        if (d.cur > h_max) {
            d.cur = 0;
            overflow = true;
        }
    } else if (d.cur > d.max_val) {
        d.cur = 0;
        overflow = true;
    }
    
    d.anim_frame = 0;
    
    if (overflow) {
        advance(idx + 1);
    }
}

void FlipClockPage::blit_half(const uint16_t *src, int cx, int cy) {
    for (int y = 0; y < HALF; y++) {
        _display.getTFT().pushImage(cx, cy + y, CW, 1, (uint16_t*)&src[y * CW]);
    }
}

void FlipClockPage::draw_trapezoid(const uint16_t *src, const StripEntry *strips, int cx) {
    for (int i = 0; i < N_STRIPS; i++) {
        int dy_top = HALF + strips[i].dy_top;
        int dy_bot = HALF + strips[i].dy_bot;
        int dst_h = dy_bot - dy_top;
        
        if (dst_h < 1) continue;
        
        int mid = dst_h >> 1;
        int ext = strips[i].ext;
        
        for (int y = 0; y < dst_h; y++) {
            int cy = dy_top + y;
            
            if (cy < 0 || cy >= CH) continue;
            
            int src_row = 2 * i + ((y < mid) ? 0 : 1);
            _display.getTFT().pushImage(cx, cy, CW, 1, (uint16_t*)&src[src_row * CW]);
            
            for (int e = 0; e < ext; e++) {
                if (cx - 1 - e >= 0) {
                    _display.getTFT().drawPixel(cx - 1 - e, cy, CARD_BG);
                }
                if (cx + CW + e < SCREEN_WIDTH) {
                    _display.getTFT().drawPixel(cx + CW + e, cy, CARD_BG);
                }
            }
        }
    }
}

void FlipClockPage::render_card(int idx) {
    Digit &d = digits[idx];
    int cx = digit_x[idx];
    
    int cy = (SCREEN_HEIGHT - CH) / 2;
    
    if (d.anim_frame < 0 || d.anim_frame >= TOTAL_FRAMES) {
        blit_half(DIGIT_UPPER[d.cur], cx, cy);
        blit_half(DIGIT_LOWER[d.cur], cx, cy + HALF);
        
        if (d.anim_frame >= TOTAL_FRAMES) {
            d.anim_frame = -1;
        }
        return;
    }
    
    blit_half(DIGIT_UPPER[d.cur], cx, cy);
    blit_half(DIGIT_LOWER[d.old], cx, cy + HALF);
    
    if (d.anim_frame < ANIM_HALF) {
        draw_trapezoid(DIGIT_UPPER[d.old], flip_table[d.anim_frame].upper, cx);
    } else {
        draw_trapezoid(DIGIT_LOWER[d.cur], flip_table[TOTAL_FRAMES - 1 - d.anim_frame].lower, cx);
    }
}

void FlipClockPage::render_colon(int cx) {
    int cy = (SCREEN_HEIGHT - CH) / 2;
    int y1 = cy + CH * 3 / 10;
    int y2 = cy + CH * 7 / 10;
    
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (dx * dx + dy * dy > 5) continue;
            
            if (y1 + dy >= 0 && y1 + dy < SCREEN_HEIGHT) {
                _display.getTFT().drawPixel(cx + dx, y1 + dy, COLON_CLR);
            }
            if (y2 + dy >= 0 && y2 + dy < SCREEN_HEIGHT) {
                _display.getTFT().drawPixel(cx + dx, y2 + dy, COLON_CLR);
            }
        }
    }
}