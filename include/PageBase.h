#ifndef PAGE_BASE_H
#define PAGE_BASE_H

#include <Arduino.h>

// 触摸事件类型与 TTP223Sensor 中的定义保持一致
typedef enum {
    TOUCH_NONE_BASE = 0,
    TOUCH_SHORT_BASE,
    TOUCH_LONG_BASE,
    TOUCH_DOUBLE_BASE
} PageTouchType;

/**
 * 所有显示页面的抽象基类
 *
 * 每个页面独立管理自己的：
 * - 进入/退出生命周期
 * - 周期性刷新逻辑
 * - 可选的页面内触摸事件响应
 */
class PageBase {
public:
    virtual ~PageBase() {}

    /** 页面刚刚成为当前页面时调用（用于初始化显示、重置缓存、清屏等） */
    virtual void onEnter() = 0;

    /** 页面即将离开时调用（用于资源回收等） */
    virtual void onExit() {}

    /** 每个循环调用一次：刷新动态元素（时间、WiFi、传感器等） */
    virtual void update() = 0;

    /** 用户触发触摸事件时调用（可重写以实现页面内行为） */
    virtual void onTouch(PageTouchType /*type*/) {}
};

#endif
