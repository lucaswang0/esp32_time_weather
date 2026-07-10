#include <Arduino.h>
#include "../main.h"
#include "../lcd/lcd.h"
#include "../input/input.h"
#include "menu.h"
#include "../apps/bt_gamepad.h"
#include "../apps/sd_manager.h"
#include "../apps/webserver_app.h"
#include "../apps/wifi_manager.h"
#include "../apps/streaming_player.h"

int sel = 0;
int scrollOffset = 0;
bool in_app = false;
int app_id = -1;

const MenuItem menuItems[] = {
  { "BT Gamepad",     bt_gamepad_init,   bt_gamepad_loop },
  { "WiFi Manager",   wifi_manager_init, wifi_manager_loop },
  { "SD File Manager", sd_manager_init,   sd_manager_loop },
  { "WebServer",      webserver_init,    webserver_loop },
  { "Streaming Player", streaming_player_init, streaming_player_loop },
};
const int MENU_ITEMS = sizeof(menuItems) / sizeof(menuItems[0]);

static void drawItem(int idx, int row) {
  int y = 28 + row * 20;
  bool active = (idx == sel);
  fill_rect(0, y, 160, 16, BLACK);
  char buf[24];
  snprintf(buf, sizeof(buf), "%s%d. %s", active ? "*" : " ", idx + 1, menuItems[idx].label);
  draw_str(8, y, buf, active ? WHITE : GRAY, BLACK);
}

void show_menu() {
  in_app = false; app_id = -1;
  scrollOffset = 0;
  sel = 0;
  fill_screen(BLACK);
  draw_str_center(4, "Select App", WHITE, BLACK);
  for (int i = 0; i < VISIBLE_ITEMS && i < MENU_ITEMS; i++)
    drawItem(i, i);
  draw_str_center(98, "UP/DN:Sel A:Open", DKGRAY, BLACK);
  draw_str_center(114, "B:Back to menu", DKGRAY, BLACK);
}

void update_menu_sel() {
  if (sel < scrollOffset) scrollOffset = sel;
  if (sel >= scrollOffset + VISIBLE_ITEMS) scrollOffset = sel - VISIBLE_ITEMS + 1;
  for (int i = 0; i < VISIBLE_ITEMS && i < MENU_ITEMS; i++) {
    int idx = scrollOffset + i;
    if (idx < MENU_ITEMS) drawItem(idx, i);
  }
}

void enter_app(int i) {
  in_app = true;
  app_id = i;
  fill_screen(BLACK);
  menuItems[i].init();
  if (menuItems[i].loop == nullptr) {
    show_menu();
  }
}