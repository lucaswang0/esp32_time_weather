#ifndef FLIP_CLOCK_PAGE_H
#define FLIP_CLOCK_PAGE_H

#include "PageBase.h"
#include "DisplayManager.h"
#include "digitals.h"
#include <TFT_eSPI.h>

// 翻页时钟页面
// 使用 TFT_eSprite 离屏渲染 + 2x/多倍缩放输出
// 字体尺寸基于 32x48 位图 (digitals.h)，可通过 scale 倍数放大
class FlipClockPage : public PageBase {
public:
    // 构造函数
    // display: DisplayManager 引用
    // area_x/y/w/h: 时钟显示区域 (屏幕坐标)，默认全屏
    FlipClockPage(DisplayManager& display,
                  int16_t area_x = 0, int16_t area_y = 0,
                  int16_t area_w = SCREEN_WIDTH, int16_t area_h = SCREEN_HEIGHT);
    ~FlipClockPage();

    void onEnter() override;   // 进入页面: 创建 sprite, 初始化时钟
    void onExit() override;    // 退出页面: 释放 sprite
    void update() override;    // 每帧更新: 动画推进 + 时间同步
    void onTouch(PageTouchType type) override;  // 触摸事件 (预留)

    // 设置时钟显示区域 (运行时调用)
    void setDisplayArea(int16_t x, int16_t y, int16_t w, int16_t h);
    // 设置字体缩放倍数 (1=原始32x48, 2=64x96, ...)
    // 设置后 calc_layout 使用该倍数而非自动计算
    void setScale(int16_t scale);

private:
    DisplayManager& _display;     // 显示管理器引用
    TFT_eSprite* _sprite;         // 离屏画布 (widget 分辨率)
    int16_t _sprite_x, _sprite_y; // sprite 在屏幕上的起始坐标
    bool _sprite_ok;              // sprite 创建成功标志

    int16_t _area_x, _area_y, _area_w, _area_h;  // 显示区域
    int16_t _vis_scale;            // 视觉缩放倍数 (1x/2x/3x...)
    bool _custom_scale;            // 是否使用用户指定的 scale

    // ===== 字体几何常量 (基于 32x48 位图) =====
    static const int CW = 32;          // 单字宽度 (widget 像素)
    static const int CH = 48;          // 单字高度 (widget 像素)
    static const int HALF = 24;        // 字高的一半 (上下翻盖分界)
    static const int GAP = 4;          // 字间距
    static const int COLON_W = 8;      // 冒号占位宽度
    static const int N_STRIPS = 12;    // 翻页动画条带数

    // ===== 动画参数 =====
    static const int FPS = 20;         // 动画帧率
    static const int TOTAL_FRAMES = 20; // 翻页总帧数
    static const int ANIM_HALF = 10;   // 前半段帧数 (翻上半)

    // ===== 颜色 =====
    static const uint16_t BG_COLOR = 0x0000;   // 背景: 纯黑
    static const uint16_t COLON_CLR = 0x8C51; // 冒号: 紫灰色

    int16_t _digit_x[6];   // 6 位数字的屏幕坐标
    int16_t _colon_x[2];   // 2 个冒号的屏幕坐标

    // 翻页条带入口 (预计算 dy_top, dy_bot, 横向扩展)
    struct StripEntry {
        int8_t dy_top;  // 条带上边缘偏移 (相对 HALF)
        int8_t dy_bot;  // 条带下边缘偏移
        int8_t ext;     // 右侧扩展像素数 (翻页时的梯形效果)
    };

    // 翻页动画帧数据
    struct FlipEntry {
        int8_t vis;                     // 当前可见高度
        StripEntry upper[N_STRIPS];     // 上半部分翻页条带
        StripEntry lower[N_STRIPS];     // 下半部分翻页条带
    };

    FlipEntry _flip_table[ANIM_HALF];  // 预计算的翻页动画表

    // 数字状态
    struct Digit {
        uint8_t cur;        // 当前显示值
        uint8_t max_val;    // 最大值 (用于限制)
        uint8_t old;        // 翻页前的旧值
        int8_t anim_frame;  // 当前动画帧 (-1=静止, 0..TOTAL_FRAMES=动画中)
    };

    Digit _digits[6];  // 6 位数字 (HH:MM:SS)

    uint32_t _next_render;  // 下次渲染时间戳

    // ===== 私有方法 =====
    void calc_layout();      // 计算布局: 坐标 + scale
    void build_flip_table(); // 预计算翻页动画表
    void init_time();        // 初始化时钟数字
    void sync_time();        // 同步系统时间到数字
    void render_frame();     // 渲染完整一帧到 sprite
    void render_card(int idx);       // 渲染单个数字 (含翻页动画)
    void render_trapezoid(int cx, const uint16_t *src, const StripEntry *strips); // 梯形翻页条带
    void render_colon(int idx);     // 渲染冒号
    void pushSpriteScaled();        // 按 scale 缩放 sprite 到屏幕
};

#endif
