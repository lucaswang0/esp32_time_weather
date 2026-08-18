// PageManager.cpp
// 页面切换管理

#include "PageManager.h"
#include "TempPage.h"
#include "CalendarPage.h"
#include "ForecastPage.h"
#include "PressurePage.h"
#include "HistoryPage.h"
#include "WiFiInfoPage.h"
#include "APModePage.h"
#include "StreamingPlayerPage.h"
#include <esp_log.h>

static const char* PAGE_NAMES[] = {"温度", "3天预报", "月历","气压", "历史", "网络信息", "AP配网", "流媒体", "无线显示"};

static const char* TAG = "PageManager";

PageManager::PageManager(DisplayManager& disp)
 : _display(disp) {
    for (int i = 0; i < PAGE_COUNT; i++) {
        _pages[i] = nullptr;
    }
    _current = PAGE_TEMP;
}

void PageManager::registerPage(PageMode mode, PageBase* page) {
    if (mode < PAGE_COUNT) {
        _pages[mode] = page;
    }
}

void PageManager::begin() {
    _pages[_current]->onEnter();
    ESP_LOGI(TAG, "current = %s", PAGE_NAMES[_current]);
}

void PageManager::begin(PageMode initialMode) {
    if (initialMode < PAGE_COUNT && _pages[initialMode] != nullptr) {
        _current = initialMode;
    }
    _pages[_current]->onEnter();
    ESP_LOGI(TAG, "current = %s", PAGE_NAMES[_current]);
}

void PageManager::next() {
    PageMode startMode = _current;
    // 从 AP_MODE 短按 → 直接跳到 TempPage（退出 AP 配置）
    if (_current == PAGE_AP_MODE) {
        if (_pages[PAGE_TEMP] != nullptr) {
            switchTo(PAGE_TEMP);
        }
        return;
    }
    PageMode nextMode = (PageMode)((_current + 1) % PAGE_COUNT);
    while ((_pages[nextMode] == nullptr || nextMode == PAGE_AP_MODE) && nextMode != startMode) {
        nextMode = (PageMode)((nextMode + 1) % PAGE_COUNT);
    }
    if (nextMode == startMode && _pages[startMode] == nullptr) {
        return;
    }
    switchTo(nextMode);
}

void PageManager::prev() {
    PageMode startMode = _current;
    // 从 AP_MODE 双击 → 直接跳到 TempPage（退出 AP 配置）
    if (_current == PAGE_AP_MODE) {
        if (_pages[PAGE_TEMP] != nullptr) {
            switchTo(PAGE_TEMP);
        }
        return;
    }
    PageMode prevMode = (PageMode)((_current + PAGE_COUNT - 1) % PAGE_COUNT);
    while ((_pages[prevMode] == nullptr || prevMode == PAGE_AP_MODE) && prevMode != startMode) {
        prevMode = (PageMode)((prevMode + PAGE_COUNT - 1) % PAGE_COUNT);
    }
    if (prevMode == startMode && _pages[startMode] == nullptr) {
        return;
    }
    switchTo(prevMode);
}

void PageManager::switchTo(PageMode mode) {
    if (mode >= PAGE_COUNT) return;
    if (_pages[mode] == nullptr) return; // 跳过未注册
    if (mode == _current) return;

    _display.fadeOut(200);
    _pages[_current]->onExit();
    _current = mode;
    _pages[_current]->onEnter();
    _display.fadeIn(200);

    ESP_LOGI(TAG, "switched to %s", PAGE_NAMES[_current]);
}

void PageManager::update() {
    _pages[_current]->update();
}

void PageManager::dispatchTouch(PageTouchType type) {
    _pages[_current]->onTouch(type);
}
