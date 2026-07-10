#ifndef AP_MODE_PAGE_H
#define AP_MODE_PAGE_H

#include "PageBase.h"
#include "DisplayManager.h"
#include "WiFiManager.h"
#include <TFT_eSPI.h>

class APModePage : public PageBase {
public:
    APModePage(DisplayManager& display, WiFiManager& wifi);
    void onEnter() override;
    void onExit() override;
    void update() override;

private:
    DisplayManager& _display;
    WiFiManager& _wifi;
    unsigned long _apStartTime;
    unsigned long _lastDrawTime;
    bool _firstDraw;
    static const unsigned long AP_TIMEOUT_MS = 10 * 60 * 1000;
    
    int getRemainingSeconds();
    void drawStaticContent(TFT_eSPI& tft);
    void updateCountdown(TFT_eSPI& tft);
};

#endif