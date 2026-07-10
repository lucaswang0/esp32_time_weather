#ifndef WIFI_INFO_PAGE_H
#define WIFI_INFO_PAGE_H

#include "PageBase.h"
#include "DisplayManager.h"
#include "WiFiManager.h"
#include <TFT_eSPI.h>

class WiFiInfoPage : public PageBase {
public:
    WiFiInfoPage(DisplayManager& display, WiFiManager& wifi);
    void onEnter() override;
    void onExit() override;
    void update() override;

private:
    DisplayManager& _display;
    WiFiManager& _wifi;
    bool _firstDraw;
    unsigned long _lastUpdateTime;
    
    void drawStaticContent(TFT_eSPI& tft);
    void updateDynamicContent(TFT_eSPI& tft);
};

#endif