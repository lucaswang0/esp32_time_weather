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

    // 视觉布局尺寸: digit_x[0]+CW-digit_x[5] = 271+48-1 = 318 宽, CH=72 高
    // Sprite 内部尺寸 = 视觉/2 = 159×36 (~11.5KB, 比原 45.7KB 节省 75%)
    // 视觉布局不变, pushSprite 时 2× 缩放输出
    int visual_w = digit_x[0] + CW - digit_x[5];
    int visual_h = CH;
    int sprite_w = visual_w / 2;  // 159
    int sprite_h = visual_h / 2;  // 36
    _sprite_x = digit_x[5];                        // 1 (视觉位置, pushSprite 用)
    _sprite_y = (SCREEN_HEIGHT - CH) / 2;          // 49 (视觉位置, pushSprite 用)

    Serial.printf("[FlipClockPage] Sprite: %dx%d (visual %dx%d) at (%d,%d), heap: %d, maxBlock: %d\n",
                  sprite_w, sprite_h, visual_w, visual_h,
                  _sprite_x, _sprite_y, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    auto& tft = _display.getTFT();

    if (!_sprite) {
        _sprite = new TFT_eSprite(&tft);
    }

    // 安全检查: 最大连续块够不够装 sprite (~11.5KB + pushSprite 缩放临时 ~4KB)
    if (ESP.getMaxAllocHeap() < (size_t)(sprite_w * sprite_h * 2 + 4096)) {
        Serial.printf("[FlipClockPage] Sprite too large for heap (need %d, maxBlock %d), fallback\n",
                      sprite_w * sprite_h * 2, ESP.getMaxAllocHeap());
        _sprite_ok = false;
        tft.setSwapBytes(true);
    } else {
        _sprite_ok = _sprite->createSprite(sprite_w, sprite_h);
    }

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
            // TFT_eSPI 2.5.43 不支持 4 参数 pushSprite(x,y,w,h) 缩放输出
            // 用 setWindow + pushPixels 手动 2× 缩放, 视觉仍是 318×72
            pushSpriteScaled2x();
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
    // 用 sprite 半分辨率常量 (HALF_S=18, CW_S=24), 让 flip_table 直接生成 sprite 坐标
    // 渲染时 render_trapezoid 不再需要做坐标转换
    for (int f = 0; f < ANIM_HALF; f++) {
        float angle = (float)f / ANIM_HALF * (float)M_PI / 2;
        int vis = max(1, (int)roundf((float)HALF_S * cosf(angle)));
        float widen = 0.25f * sinf(angle);
        flip_table[f].vis = (int8_t)vis;

        for (int i = 0; i < N_STRIPS; i++) {
            float t = (float)i / N_STRIPS;
            float sh = (float)vis / N_STRIPS;

            flip_table[f].upper[i] = {
                (int8_t)roundf(-vis + i * sh),
                (int8_t)roundf(-vis + (i + 1) * sh),
                (int8_t)roundf(widen * CW_S * (1 - t) / 2)
            };

            flip_table[f].lower[i] = {
                (int8_t)roundf(i * sh),
                (int8_t)roundf((i + 1) * sh),
                (int8_t)roundf(widen * CW_S * t / 2)
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
        
        digits[0] = {(uint8_t)(s % 10), (uint8_t)9, (uint8_t)(s % 10), (int8_t)-1};
        digits[1] = {(uint8_t)(s / 10), (uint8_t)5, (uint8_t)(s / 10), (int8_t)-1};
        digits[2] = {(uint8_t)(m % 10), (uint8_t)9, (uint8_t)(m % 10), (int8_t)-1};
        digits[3] = {(uint8_t)(m / 10), (uint8_t)5, (uint8_t)(m / 10), (int8_t)-1};
        digits[4] = {(uint8_t)(h % 10), (uint8_t)9, (uint8_t)(h % 10), (int8_t)-1};
        digits[5] = {(uint8_t)(h / 10), (uint8_t)2, (uint8_t)(h / 10), (int8_t)-1};
    } else {
        for (int i = 0; i < 6; i++) digits[i] = {(uint8_t)0, (uint8_t)9, (uint8_t)0, (int8_t)-1};
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

// 把一个源 RGB565 像素渲染到 sprite 位置上, 但**过滤掉源位图底色**
// 让数字周边在 sprite 内部保持 BG_COLOR (透明效果), 缩放输出后即"黑底".
//
// 源位图颜色分布 (digitals.h):
//   底色: 0x0841 / 0x1082 / 0x10A2 / 0x18C3 (R/G/B max ≤ 12 ≈ 1.5/8)
//   笔画: 0x2945 / 0x39E7 / 0x6B6D / 0x8C51 / 0xCE79 (R/G/B max ≥ 18 ≈ 2/8)
// 阈值取 16: max 5-bit 通道 <= 16 当背景过滤掉.
static inline bool is_source_background(uint16_t px) {
    // RGB565 高 5 位是亮度, 取 R/G/B 三个 5-bit 通道的最大值
    uint8_t r5 = (px >> 11) & 0x1F;
    uint8_t g5 = (px >> 6) & 0x1F;
    uint8_t b5 = (px >> 0) & 0x1F;
    uint8_t mx = r5;
    if (g5 > mx) mx = g5;
    if (b5 > mx) mx = b5;
    return mx <= 16;  // 0x18C3 R=3/G=12/B=3 → max=12 ≤ 16 ✓;  0x39E7 max=26 > 16 ✓
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
            uint16_t raw16 = src[sy * src_pitch + sx];

            // ⭐ 关键: 过滤源位图底色 (0x0841/0x1082/0x10A2/0x18C3)
            // 不写入 sprite, 保留背景黑色, 数字周边 = 全黑
            if (is_source_background(raw16)) continue;

            uint16_t pixel = (raw16 >> 8) | (raw16 << 8);

            int dx0 = dst_x + sx * max_x / (src_w - 1);
            int dx1 = dst_x + (sx + 1) * max_x / (src_w - 1);
            if (dx0 == dx1) dx1 = dx0 + 1;

            for (int dy = dy0; dy < dy1; dy++) {
                // ⭐ 关键: 边界检查必须用 sprite 实际高度 CH_S=36, 不能用 dst_h
                // push_scaled 旧版 (sprite=318x72 时) 用 max_y+1=dst_h=36 凑巧等于 sprite 高度, 没出 bug
                // 半分辨率 sprite=159x36 后, dst_h=HALF_S=18 但 sprite 高度是 36
                // 上半 (dst_y=0): dy 0-17, 18 边界 OK
                // 下半 (dst_y=18): dy 18-35, 旧代码 dy>=18 全部 skip → 下半消失!
                if (dy < 0 || dy >= CH_S) continue;
                for (int dx = dx0; dx < dx1; dx++) {
                    if (dx >= 0 && dx < sp_w) {
                        buf[dy * sp_w + dx] = pixel;
                    }
                }
            }
        }
    }
}

// 替代 TFT_eSprite::pushSprite(x, y, w, h) 缩放输出
// TFT_eSPI 2.5.43 只支持 2/3/6 参数 pushSprite, 4 参数 (含输出缩放) 是 2.5.44+ 才有
// 用 setWindow 一次开窗 + pushPixels 逐行写入, 行内手动 2× 复制实现缩放
// 性能: 36 行 × 2 次 pushPixels = 72 次 SPI 传输, 比 72 次 pushImage 快 (开窗只有 1 次)
//
// 字节序说明: sprite buffer 已经是"显示就绪"字节序 (push_scaled 写入时已字节交换)
// 模仿 _sprite->pushSprite 的做法: 临时 tft.setSwapBytes(false) 让 pushPixels 直发
// 此时 buffer 里的 [low,high] 内存顺序正好 = 显示器期望的 [high,low] 接收顺序
void FlipClockPage::pushSpriteScaled2x() {
    if (!_sprite_ok) return;

    int sw = _sprite->width();   // 159
    int sh = _sprite->height();  // 36
    int dw = sw * 2;             // 318
    int dh = sh * 2;             // 72

    auto& tft = _display.getTFT();
    uint16_t* src = (uint16_t*)_sprite->getPointer();

    // 临时关闭 swapBytes (与 _sprite->pushSprite 内部行为一致), 退出前恢复
    bool oldSwapBytes = tft.getSwapBytes();
    tft.setSwapBytes(false);

    // ⭐ 关键: startWrite/endWrite 包裹, 拉低 CS 让 SPI 数据真的能到 display
    // setWindow 注释明确说 "begin_tft_write() must be called before setWindow"
    // 否则 CS 一直高, display 静默丢弃所有 pushPixels 数据
    tft.startWrite();

    // 一次性开窗覆盖整个 2× 输出区域
    tft.setWindow(_sprite_x, _sprite_y, _sprite_x + dw - 1, _sprite_y + dh - 1);

    // 行缓冲区 (栈分配, 320 像素 = 640 字节, 足够装 318 像素的 2× 行)
    uint16_t row_buf[SCREEN_WIDTH];

    for (int sy = 0; sy < sh; sy++) {
        uint16_t* sp = &src[sy * sw];
        // 横向 2× 复制 (sprite buffer 已是显示就绪字节序, 直接复制即可)
        for (int i = 0; i < sw; i++) {
            row_buf[i * 2]     = sp[i];
            row_buf[i * 2 + 1] = sp[i];
        }
        // 纵向 2× 复制: 同一行写 2 次
        tft.pushPixels(row_buf, dw);
        tft.pushPixels(row_buf, dw);
    }

    tft.endWrite();
    tft.setSwapBytes(oldSwapBytes);
}

void FlipClockPage::render_card(int idx) {
    Digit &d = digits[idx];
    // cx 在 sprite 内部坐标 = (视觉位置差) / 2
    int cx = _sprite_ok ? (digit_x[idx] - _sprite_x) / 2 : digit_x[idx];
    int scr_cx = digit_x[idx];
    int scr_cy = (SCREEN_HEIGHT - CH) / 2;

    if (d.anim_frame < 0 || d.anim_frame >= TOTAL_FRAMES) {
        if (_sprite_ok) {
            push_scaled(cx, 0,       SRC_W, SRC_HALF, CW_S, HALF_S, (uint16_t*)DIGIT_UPPER[d.cur], SRC_W);
            push_scaled(cx, HALF_S,  SRC_W, SRC_HALF, CW_S, HALF_S, (uint16_t*)DIGIT_LOWER[d.cur], SRC_W);
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
        push_scaled(cx, 0,       SRC_W, SRC_HALF, CW_S, HALF_S, (uint16_t*)DIGIT_UPPER[d.cur], SRC_W);
        push_scaled(cx, HALF_S,  SRC_W, SRC_HALF, CW_S, HALF_S, (uint16_t*)DIGIT_LOWER[d.old], SRC_W);

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
    int max_x = CW_S - 1;

    for (int i = 0; i < N_STRIPS; i++) {
        int dy_top = HALF_S + strips[i].dy_top;
        int dy_bot = HALF_S + strips[i].dy_bot;
        int dst_h = dy_bot - dy_top;

        if (dst_h < 1) {
            dy_bot = dy_top + 1;
            dst_h = 1;
        }

        int mid = dst_h >> 1;
        int ext = strips[i].ext;

        for (int y = 0; y < dst_h; y++) {
            int dst_row = dy_top + y;

            if (dst_row < 0 || dst_row >= CH_S) continue;

            int src_row = 2 * i + ((y < mid) ? 0 : 1);

            for (int x = 0; x < SRC_W; x++) {
                uint16_t raw = src[src_row * SRC_W + x];

                // ⭐ 与 push_scaled 一致: 过滤源位图底色, 翻页过程显示区域保持黑色
                if (is_source_background(raw)) continue;

                uint16_t pixel = (raw >> 8) | (raw << 8);
                int dx0 = cx + x * max_x / (SRC_W - 1);
                int dx1 = cx + (x + 1) * max_x / (SRC_W - 1);
                if (dx0 == dx1) dx1 = dx0 + 1;

                int dy0 = dst_row;
                int dy1 = dst_row + 1;
                if (dy0 == dy1) dy1 = dy0 + 1;

                for (int dy = dy0; dy < dy1; dy++) {
                    if (dy < 0 || dy >= CH_S) continue;
                    for (int dx = dx0; dx < dx1; dx++) {
                        if (dx >= 0 && dx < sp_w) {
                            buf[dy * sp_w + dx] = pixel;
                        }
                    }
                }
            }

            int ext_dx_start = cx + CW_S;
            int ext_dx_end = ext_dx_start + (ext * (CW_S - 1)) / (SRC_W - 1);
            for (int dx = ext_dx_start; dx < ext_dx_end && dx < sp_w; dx++) {
                int dy0 = dst_row;
                int dy1 = dst_row + 1;
                if (dy0 == dy1) dy1 = dy0 + 1;
                for (int dy = dy0; dy < dy1; dy++) {
                    if (dy >= 0 && dy < CH_S) {
                        buf[dy * sp_w + dx] = (uint16_t)((BG_COLOR >> 8) | (BG_COLOR << 8));
                    }
                }
            }
        }
    }
}

void FlipClockPage::render_colon(int idx) {
    // cx 在 sprite 内部坐标 = (视觉位置差) / 2
    int cx = _sprite_ok ? (colon_x[idx] - _sprite_x) / 2 : colon_x[idx];
    int scr_cx = colon_x[idx];
    int scr_cy = (SCREEN_HEIGHT - CH) / 2;
    int y1 = CH * 3 / 10;
    int y2 = CH * 7 / 10;
    int r = 3;

    // 关键修复 (2026-07-26): 冒号被拉长问题
    // - 上一版 dx 用 (CW_S-1)/(SRC_W-1) ≈ 0.74 但 dy 用 (CH_S-1)/(HALF_S-1) ≈ 2.06
    //   → sprite 内圆变成"纵向 12.4 像素 × 横向 4.4 像素"椭圆 (横纵比不一致)
    // - 2x pushSprite 缩放后视觉上"拉长"
    // 修复: dx, dy 用同一个缩放因子, 即 (CW_S-1)/(SRC_W-1), 横纵比 1:1
    //
    // 圆心 y 坐标: 视觉 y1=21, y2=50 → sprite y1_s=10, y2_s=25 (CH_S=36 内)
    // 半分辨率 sprite 高 36, 圆心相距 15 像素 → 缩放后屏幕相距 30 屏像素 (合理)
    int r_s = r * (CW_S - 1) / (SRC_W - 1);  // sprite 像素半径
    if (r_s < 1) r_s = 1;
    int y1_s = CH_S * 3 / 10;  // 10
    int y2_s = CH_S * 7 / 10;  // 25

    if (_sprite_ok) {
        // 圆点在 sprite 中画
        // 先清掉之前的, 用 fillSprite 已铺 BG, 直接 drawPixel 即可
        for (int dy = -r_s - 1; dy <= r_s + 1; dy++) {
            for (int dx = -r_s - 1; dx <= r_s + 1; dx++) {
                if (dx * dx + dy * dy > r_s * r_s) continue;

                int fx = cx + dx;
                int fy1 = y1_s + dy;
                int fy2 = y2_s + dy;
                if (fx >= 0 && fx < _sprite->width()) {
                    if (fy1 >= 0 && fy1 < CH_S) _sprite->drawPixel(fx, fy1, COLON_CLR);
                    if (fy2 >= 0 && fy2 < CH_S) _sprite->drawPixel(fx, fy2, COLON_CLR);
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
