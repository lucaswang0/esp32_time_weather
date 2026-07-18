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
 * 页面切换负责控制哪个页面显示。
 * 仅承担分发，不做具体绘制。
 */
class PageManager {
public:
    enum PageMode {
        PAGE_TEMP = 0,
        PAGE_FORECAST,
        PAGE_CALENDAR,
        PAGE_PRESSURE,   // 新增：气压页面
        PAGE_HISTORY,
        PAGE_WIFI_INFO,  // 新增：网络信息页面
        PAGE_AP_MODE,
        PAGE_STREAMING,  // 新增：流媒体播放器页面
        PAGE_COUNT
    };

    PageManager(DisplayManager& disp);

    void registerPage(PageMode mode, PageBase* page);

    void begin();                       // 启动：进入第一个页面
    void begin(PageMode initialMode);   // 启动：进入指定初始页面
    PageMode current() const { return _current; }
    void next();                        // 切换到下一个页面
    void prev();                        // 切换到上一个页面
    void switchTo(PageMode mode);       // 切换到指定页面

    void update();                      // 调用当前页 update()
    void dispatchTouch(PageTouchType type);  // 转发触摸到当前页

private:
    PageBase* _pages[PAGE_COUNT];
    PageMode _current;
    DisplayManager& _display;
};

#endif
