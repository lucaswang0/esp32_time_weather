#ifndef FLIP_CLOCK_PAGE_H
#define FLIP_CLOCK_PAGE_H

#include "PageBase.h"
#include "DisplayManager.h"
#include "digitals.h"
#include <TFT_eSPI.h>

class FlipClockPage : public PageBase {
public:
    FlipClockPage(DisplayManager& display);
    ~FlipClockPage();
    
    void onEnter() override;
    void onExit() override;
    void update() override;
    void onTouch(PageTouchType type) override;

private:
    DisplayManager& _display;
    TFT_eSprite* _sprite;
    int16_t _sprite_x, _sprite_y;
    bool _sprite_ok;
    
    static const int SRC_W = 32;     // Source bitmap width (from digitals.h)
    static const int SRC_HALF = 24;  // Source half height
    static const int CW = 48;       // 1.5x display scale (visual on screen)
    static const int CH = 72;       // 1.5x display scale (visual on screen)
    static const int HALF = 36;     // 1.5x display scale (visual on screen)
    static const int GAP = 2;
    static const int COLON_W = 8;
    static const int N_STRIPS = 12;
    static const int FPS = 20;
    static const int TOTAL_FRAMES = 20;
    static const int ANIM_HALF = 10;

    // 半分辨率 sprite 内部坐标 (pushSprite 时 2× 缩放输出到屏幕)
    // 节省内存: sprite 163×36 = 11.5KB (原 318×72 = 45.7KB, 节省 75%)
    static const int CW_S = CW / 2;     // 24
    static const int CH_S = CH / 2;     // 36
    static const int HALF_S = HALF / 2; // 18
    
    // FlipClockPage 经典配色: 黑底 + 白数字 + 白冒号
    // 之前 0x3333 (R=6/G=25/B=19) 实际显示为几乎纯黑, 0x8C51 (R=17/G=34/B=17) 显示为偏白
    // 改用 TFT_BLACK (0x0000) 和 TFT_WHITE (0xFFFF) 让颜色符合"黑底白字"翻页钟预期
    static const uint16_t BG_COLOR = 0x0000;     // TFT_BLACK
    static const uint16_t COLON_CLR = 0xFFFF;    // TFT_WHITE
    
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
    void sync_time();
    void render_frame();
    void render_card(int idx);
    void render_trapezoid(int cx, const uint16_t *src, const StripEntry *strips);
    void render_colon(int idx);
    void push_scaled(int dst_x, int dst_y, int src_w, int src_h, int dst_w, int dst_h, const uint16_t *src, int src_pitch);
    void pushSpriteScaled2x();  // TFT_eSPI 2.5.43 不支持 4 参数 pushSprite, 手动 2× 缩放输出
};

#endif
