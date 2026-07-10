#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "../main.h"
#include "../lcd/lcd.h"
#include "../menu/menu.h"
#include "wifi_manager.h"

static const int VISIBLE = 3;
static int scannedCount = 0;
static String ssids[50];
static int8_t rssi[50];
static uint8_t enc[50];
static int wifiSel = 0;
static int wifiScroll = 0;
static int modeSel = 0;

static unsigned long connStart = 0;
static String connSSID;
static unsigned long lastBtnT = 0;
static Preferences prefs;

static String getNvsKey(const String& ssid) {
  uint32_t hash = 5381;
  for (int i = 0; i < ssid.length(); i++) {
    hash = ((hash << 5) + hash) + ssid[i];
  }
  char buf[9];
  snprintf(buf, sizeof(buf), "%08X", hash);
  return String(buf);
}

enum State { MODE_SELECT, MSG, LIST, CONNECTING, CONNECTED, FAILED, NO_PWD, CONFIRM_EXIT };
static State st;
static const char* msgLine1 = "";
static const char* msgLine2 = "";
static const char* msgLine3 = "";

static void drawModeSelect() {
  fill_screen(BLACK);
  draw_str_center(4, "WiFi Manager", WHITE, BLACK);
  const char* opt1 = (wifi_state == WIFI_STATE_AP) ? "Stop Hotspot" : "Start Hotspot";
  const char* opt2 = "Join Network";
  for (int i = 0; i < 2; i++) {
    int y = 40 + i * 24;
    bool active = (i == modeSel);
    fill_rect(0, y, 160, 18, BLACK);
    char buf[24];
    snprintf(buf, sizeof(buf), "%s%d. %s", active ? "*" : " ", i + 1, i == 0 ? opt1 : opt2);
    draw_str(8, y, buf, active ? WHITE : GRAY, BLACK);
  }
  draw_str_center(110, "B:Back to menu", DKGRAY, BLACK);
}

static void showMsg(const char* l1, const char* l2, const char* l3) {
  msgLine1 = l1; msgLine2 = l2; msgLine3 = l3;
  fill_screen(BLACK);
  draw_str_center(30, msgLine1, WHITE, BLACK);
  draw_str_center(52, msgLine2, GRAY, BLACK);
  draw_str_center(74, msgLine3, GRAY, BLACK);
  draw_str_center(110, "A:Continue  B:Menu", DKGRAY, BLACK);
  
}

static void drawList() {
  fill_screen(BLACK);
  draw_str_center(4, "Join Network", WHITE, BLACK);

  int n = scannedCount;
  if (n == 0) {
    draw_str_center(50, "No networks found", GRAY, BLACK);
    draw_str_center(98, "A:Rescan  B:Back", DKGRAY, BLACK);
    
    return;
  }

  for (int i = 0; i < VISIBLE && (wifiScroll + i) < n; i++) {
    int idx = wifiScroll + i;
    int y = 28 + i * 20;
    bool active = (idx == wifiSel);
    fill_rect(0, y, 146, 16, BLACK);
    char buf[28];
    bool isConnected = (wifi_state == WIFI_STATE_STA && ssids[idx] == WiFi.SSID());
    snprintf(buf, sizeof(buf), "%s%d. %s%s", active ? "*" : " ", idx + 1, ssids[idx].c_str(), isConnected ? " \x10" : "");
    draw_str(8, y, buf, isConnected ? GREEN : (active ? WHITE : GRAY), BLACK);
  }
  draw_str_center(98, "UP/DN:Sel A:Connect B:Back", DKGRAY, BLACK);
  
}

static void updateSel() {
  if (wifiSel < wifiScroll) wifiScroll = wifiSel;
  if (wifiSel >= wifiScroll + VISIBLE) wifiScroll = wifiSel - VISIBLE + 1;
  int n = scannedCount;
  for (int i = 0; i < VISIBLE && (wifiScroll + i) < n; i++) {
    int idx = wifiScroll + i;
    int y = 28 + i * 20;
    bool active = (idx == wifiSel);
    fill_rect(0, y, 146, 16, BLACK);
    char buf[28];
    bool isConnected = (wifi_state == WIFI_STATE_STA && ssids[idx] == WiFi.SSID());
    snprintf(buf, sizeof(buf), "%s%d. %s%s", active ? "*" : " ", idx + 1, ssids[idx].c_str(), isConnected ? " \x10" : "");
    draw_str(8, y, buf, isConnected ? GREEN : (active ? WHITE : GRAY), BLACK);
  }
}

static void doConnect() {
  st = CONNECTING;
  connStart = millis();
  fill_screen(BLACK);
  draw_str_center(40, "Connecting...", WHITE, BLACK);
  draw_str_center(56, connSSID.c_str(), GRAY, BLACK);
  draw_str_center(98, "B:Cancel", DKGRAY, BLACK);
  
}

static void startConnect(int i) {
  connSSID = ssids[i];
  if (enc[i] == 0) {
    WiFi.begin(connSSID.c_str());
    doConnect();
    return;
  }
  prefs.begin("wifi_pwd", true);
  Serial.printf("[WIFI] Looking for SSID: '%s' (len=%d)\n", connSSID.c_str(), connSSID.length());
  String key = getNvsKey(connSSID);
  Serial.printf("[WIFI] NVS key: '%s'\n", key.c_str());
  String pwd = prefs.getString(key.c_str(), "");
  Serial.printf("[WIFI] Found pwd len: %d\n", pwd.length());
  prefs.end();
  if (pwd.length() > 0) {
    WiFi.begin(connSSID.c_str(), pwd.c_str());
    doConnect();
  } else {
    st = NO_PWD;
    fill_screen(BLACK);
    draw_str_center(30, "No password saved!", WHITE, BLACK);
    draw_str_center(52, connSSID.c_str(), GRAY, BLACK);
    draw_str_center(74, "Go to WebServer", GRAY, BLACK);
    draw_str_center(90, "to set password", GRAY, BLACK);
    draw_str_center(110, "A:OK  B:Back to menu", DKGRAY, BLACK);
}
}

void wifi_manager_init() {
  sel = 0;
  wifiSel = 0;
  wifiScroll = 0;
  modeSel = 0;
  st = MODE_SELECT;
  drawModeSelect();
}

void wifi_manager_loop() {
  unsigned long n = millis();

  if (st == CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      wifi_state = WIFI_STATE_STA;
      st = CONNECTED;
      fill_screen(BLACK);
      draw_str_center(24, "Connected!", GREEN, BLACK);
      char buf[24];
      snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
      draw_str_center(44, buf, WHITE, BLACK);
      draw_str_center(98, "A:List  B:Exit", DKGRAY, BLACK);
      
    }
    else if (n - connStart > 10000) {
      WiFi.disconnect();
      wifi_state = WIFI_STATE_OFF;
      st = FAILED;
      fill_screen(BLACK);
      draw_str_center(40, "Connection failed", WHITE, BLACK);
      draw_str(0, 60, connSSID.c_str(), GRAY, BLACK);
      draw_str_center(98, "A:Retry  B:Back to menu", DKGRAY, BLACK);
      
    }
    if (n - lastBtnT < 200) return;
    bool b = digitalRead(12) == LOW;
    if (!b) return;
    lastBtnT = n;
    WiFi.disconnect();
    st = LIST;
    drawList();
    return;
  }

  if (n - lastBtnT < 200) return;

  bool up = digitalRead(2) == LOW;
  bool dn = digitalRead(13) == LOW;
  bool a  = digitalRead(34) == LOW;
  bool b  = digitalRead(12) == LOW;

  if (!up && !dn && !a && !b) return;
  lastBtnT = n;

  if (st == MODE_SELECT) {
    if (up && modeSel > 0) { modeSel--; drawModeSelect(); }
    else if (dn && modeSel < 1) { modeSel++; drawModeSelect(); }
    else if (a) {
      if (modeSel == 0) {
        if (wifi_state == WIFI_STATE_AP) {
          WiFi.softAPdisconnect(true);
          wifi_state = WIFI_STATE_OFF;
          st = MSG;
          showMsg("Hotspot stopped", "", "");
        } else {
          WiFi.mode(WIFI_MODE_AP);
          WiFi.softAP("ESP32-WIFI");
          delay(500);
          wifi_state = WIFI_STATE_AP;
          st = MSG;
          showMsg("Hotspot started!", "SSID: ESP32-WIFI", "IP: 192.168.4.1");
        }
      } else {
        fill_screen(BLACK);
        draw_str_center(40, "Scanning WiFi...", WHITE, BLACK);
        
        WiFi.mode(WIFI_MODE_STA);
        WiFi.disconnect();
        delay(100);
        scannedCount = WiFi.scanNetworks();
        if (scannedCount < 0) scannedCount = 0;
        if (scannedCount > 50) scannedCount = 50;
        for (int i = 0; i < scannedCount; i++) {
          ssids[i] = WiFi.SSID(i);
          rssi[i] = WiFi.RSSI(i);
          enc[i] = WiFi.encryptionType(i);
        }
        wifiSel = 0;
        wifiScroll = 0;
        st = LIST;
        drawList();
      }
    }
    else if (b) { show_menu(); }
  }
  else if (st == MSG) {
    if (a) { show_menu(); }
    else if (b) { show_menu(); }
  }
  else if (st == LIST) {
    if (up && wifiSel > 0) { wifiSel--; updateSel(); }
    else if (dn && wifiSel < scannedCount - 1) { wifiSel++; updateSel(); }
    else if (a) {
      if (scannedCount == 0) {
        st = MODE_SELECT;
        drawModeSelect();
      } else {
        startConnect(wifiSel);
      }
    }
    else if (b) {
      if (wifi_state == WIFI_STATE_STA) {
        st = CONFIRM_EXIT;
        fill_screen(BLACK);
        draw_str_center(40, "A:Disconnect WiFi", WHITE, BLACK);
        draw_str_center(64, "B:Exit to menu", GRAY, BLACK);
        
      } else {
        show_menu();
      }
    }
  }
  else if (st == CONFIRM_EXIT) {
    if (a) {
      WiFi.disconnect();
      wifi_state = WIFI_STATE_OFF;
      st = LIST;
      drawList();
    }
    else if (b) { show_menu(); }
  }
  else if (st == CONNECTED) {
    if (a) { st = LIST; drawList(); }
    else if (b) { show_menu(); }
  }
  else if (st == FAILED) {
    if (a) { startConnect(wifiSel); }
    else if (b) { show_menu(); }
  }
  else if (st == NO_PWD) {
    if (a) { st = LIST; drawList(); }
    else if (b) { show_menu(); }
  }
}