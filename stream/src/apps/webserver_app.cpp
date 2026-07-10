#include <Arduino.h>
#include <WiFi.h>
#include "../main.h"
#include "../lcd/lcd.h"
#include "../menu/menu.h"
#include "../webserver/webserver.h"
#include "webserver_app.h"

static bool apStartedByUs = false;

static void drawStatus() {
  fill_screen(BLACK);
  draw_str_center(0, "WebServer", WHITE, BLACK);
  wifi_mode_t mode = WiFi.getMode();
  char buf[48];
  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
    draw_str(0, 20, "Mode: Hotspot", WHITE, BLACK);
    snprintf(buf, sizeof(buf), "IP: %s", WiFi.softAPIP().toString().c_str());
    draw_str(0, 36, buf, GRAY, BLACK);
    draw_str(0, 84, "Open browser to", GRAY, BLACK);
    draw_str_center(100, WiFi.softAPIP().toString().c_str(), WHITE, BLACK);
  } else {
    draw_str(0, 20, "Mode: Station", WHITE, BLACK);
    snprintf(buf, sizeof(buf), "SSID: %s", WiFi.SSID().c_str());
    draw_str(0, 36, buf, GRAY, BLACK);
    snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
    draw_str(0, 52, buf, GRAY, BLACK);
    draw_str(0, 84, "Open browser to", GRAY, BLACK);
    draw_str_center(100, WiFi.localIP().toString().c_str(), WHITE, BLACK);
  }
  draw_wifi_icon(158, 0, wifi_state == WIFI_STATE_STA ? GREEN : (wifi_state == WIFI_STATE_AP ? BLUE : GRAY));
}

void webserver_init() {
  in_app = true;
  app_id = 3;

  fill_screen(BLACK);
  draw_str_center(40, "Starting WebServer...", WHITE, BLACK);
  draw_wifi_icon(158, 0, wifi_state == WIFI_STATE_STA ? GREEN : (wifi_state == WIFI_STATE_AP ? BLUE : GRAY));

  apStartedByUs = false;
  if (wifi_state == WIFI_STATE_OFF) {
    draw_str_center(56, "Starting AP...", WHITE, BLACK);
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAP("ESP32-WIFI");
    delay(500);
    wifi_state = WIFI_STATE_AP;
    apStartedByUs = true;
  }

  ws_init();
  drawStatus();
}

void webserver_loop() {
  ws_handle_client();

  if (digitalRead(12) == LOW) {
    delay(200);
    if (digitalRead(12) == LOW) {
      webserver_deinit();
      show_menu();
    }
  }
}

void webserver_deinit() {
  ws_stop();
  if (apStartedByUs) {
    WiFi.softAPdisconnect(true);
    wifi_state = WIFI_STATE_OFF;
  }
}