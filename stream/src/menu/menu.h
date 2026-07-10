#pragma once
#include <stdint.h>

#define VISIBLE_ITEMS 3

struct MenuItem {
  const char* label;
  void (*init)();
  void (*loop)();
};

extern const MenuItem menuItems[];
extern const int MENU_ITEMS;

extern bool in_app;
extern int app_id;
extern int sel;
extern int scrollOffset;

void show_menu();
void update_menu_sel();
void enter_app(int i);
