#ifndef WEB_SERVER_PAGE_H
#define WEB_SERVER_PAGE_H

#include "PageBase.h"
#include "DisplayManager.h"
#include "WiFiManager.h"

class WebServerPage : public PageBase {
public:
    WebServerPage(DisplayManager& display, WiFiManager& wifi);
    ~WebServerPage();
    
    void onEnter() override;
    void onExit() override;
    void update() override;

private:
    DisplayManager& _display;
    WiFiManager& _wifi;
    bool _serverStarted;
};

#endif