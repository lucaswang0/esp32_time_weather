#ifndef PRESSURE_PAGE_H
#define PRESSURE_PAGE_H

#include "PageBase.h"
#include "DisplayManager.h"
#include "AHT20BMP280Sensor.h"

class PressurePage : public PageBase {
public:
    PressurePage(DisplayManager& disp, AHT20BMP280Sensor& aht20);

    void onEnter() override;
    void onExit() override {}
    void update() override;

    // 供 main.cpp 调用：检查气压预警
    // 返回 true 表示触发了"警告"级别（需要自动切换页面）
    bool checkAlert();

    // 获取最近一次预警结果（供显示用）
    float getLastChange3h() const { return lastChange3h; }
    String getLastWarningLevel() const { return lastWarningLevel; }
    int getLastRecordCount() const { return lastRecordCount; }

private:
    DisplayManager& display;
    AHT20BMP280Sensor& aht20;

    // 缓存，避免不必要的重绘
    float lastPressure;
    float lastChange3h;
    String lastWarningLevel;
    int lastRecordCount;
    unsigned long lastDrawTime;

    void drawPage();
    String getTrendArrow(float change);
    void drawPressureGraph(int x, int y, int w, int h);
};

#endif
