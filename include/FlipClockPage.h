#ifndef FLIP_CLOCK_PAGE_H
#define FLIP_CLOCK_PAGE_H

#include "PageBase.h"
#include "DisplayManager.h"
#include "digitals.h"

class FlipClockPage : public PageBase {
public:
    FlipClockPage(DisplayManager& display);
    
    void onEnter() override;
    void onExit() override;
    void update() override;
    void onTouch(PageTouchType type) override;

private:
    DisplayManager& _display;
    
    static const int CW = 32;
    static const int CH = 48;
    static const int HALF = 24;
    static const int GAP = 4;
    static const int COLON_W = 8;
    static const int N_STRIPS = 12;
    static const int FPS = 20;
    static const int TOTAL_FRAMES = 20;
    static const int ANIM_HALF = 10;
    
    static const uint16_t BG_COLOR = 0x3333;
    static const uint16_t CARD_BG = 0x0841;
    static const uint16_t COLON_CLR = 0x8C51;
    
    int16_t digit_x[6];
    int16_t colon_x[2];
    
    struct StripEntry {
        int8_t dy_top, dy_bot, ext;
    };
    
    struct FlipEntry {
        int8_t vis;
        StripEntry upper[N_STRIPS];
        StripEntry lower[N_STRIPS];
    };
    
    FlipEntry flip_table[ANIM_HALF];
    
    struct Digit {
        uint8_t cur;
        uint8_t max_val;
        uint8_t old;
        int8_t anim_frame;
    };
    
    Digit digits[6];
    
    uint32_t _next_second;
    uint32_t _next_render;
    
    void calc_layout();
    void build_flip_table();
    void init_time();
    void advance(int idx);
    void blit_half(const uint16_t *src, int cx, int cy);
    void draw_trapezoid(const uint16_t *src, const StripEntry *strips, int cx);
    void render_card(int idx);
    void render_colon(int cx);
};

#endif
