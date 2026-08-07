#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include "PageBase.h"
#include "DisplayManager.h"

class TempPage;
class CalendarPage;
class ForecastPage;
class HistoryPage;
class PressurePage;
class APModePage;
class WiFiInfoPage;
class StreamingPlayerPage;

/**
 * v7.3 页面切换负责控制哪个页面显示。
 * 仅承担分发，不做具体绘制。
 * 页面数量：8（温度 / 3天预报 / 月历 / 历史 / 气压 / 网络信息 / AP配网 / 流媒体）
 */
class PageManager {
public:
    enum PageMode {
        PAGE_TEMP = 0, // 温度 / 湿度
        PAGE_FORECAST,  // 3天预报
        PAGE_CALENDAR,  // 月历
        PAGE_HISTORY,   // 历史温湿度
        PAGE_PRESSURE,  // 气压
        PAGE_WIFI_INFO, // 网络信息
        PAGE_AP_MODE,   // AP配网
        PAGE_STREAMING, // 流媒体播放器
        PAGE_COUNT
    };

    PageManager(DisplayManager& disp);

    void registerPage(PageMode mode, PageBase* page);

    void begin(); // 启动：进入第一个页面 (PAGE_TEMP)
    void begin(PageMode initialMode); // 启动：进入指定初始页面
    PageMode current() const { return _current; }
    void next(); // 切换到下一个页面
    void prev(); // 切换到上一个页面
    void switchTo(PageMode mode); // 切换到指定页面

    void update(); // 调用当前页 update()
    void dispatchTouch(PageTouchType type); // 转发触摸到当前页

private:
    PageBase* _pages[PAGE_COUNT];
    PageMode _current;
    DisplayManager& _display;
};

#endif
