#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "sd_manager.h"

#define BLACK   0x0000
#define WHITE   0xFFFF
#define GRAY    0x8410
#define DKGRAY  0x39E7
#define SEL_BG  0x0044AA

extern SPIClass spi;
extern void fill_screen(uint16_t c);
extern void fill_rect(int x, int y, int w, int h, uint16_t c);
extern void draw_str(int x, int y, const char* s, uint16_t fg, uint16_t bg);
extern void draw_str_center(int y, const char* s, uint16_t fg, uint16_t bg);
extern void show_menu();

#define MAX_ENTRIES 128

static String currentPath;
static String entries[MAX_ENTRIES];
static bool entryIsDir[MAX_ENTRIES];
static int entryCount = 0;
static int scrollOffset = 0;
static int selection = 0;

static int state = 0;
static int popupSel = 0;
static bool hasParent = false;

static const int btn_pins[6] = {2, 13, 27, 35, 34, 12};
static bool lastBtn[6] = {0};
static unsigned long lastInput = 0;

static String getParentPath(const String& path) {
  if (path == "/") return "/";
  int pos = path.lastIndexOf('/');
  if (pos <= 0) return "/";
  return path.substring(0, pos);
}

static bool btnPressed(int idx) {
  bool curr = (digitalRead(btn_pins[idx]) == LOW);
  bool ret = curr && !lastBtn[idx];
  lastBtn[idx] = curr;
  return ret;
}

static void clearRow(int y) {
  fill_rect(0, y, 160, 16, BLACK);
}

static void drawEntry(int row, int idx) {
  int y = row * 16;
  if (idx < 0 || idx >= entryCount) {
    clearRow(y);
    return;
  }
  bool sel = (idx == selection);
  uint16_t fg = sel ? WHITE : GRAY;
  if (sel) fill_rect(0, y, 160, 16, SEL_BG);
  else fill_rect(0, y, 160, 16, BLACK);
  char buf[21];
  if (entryIsDir[idx])
    snprintf(buf, sizeof(buf), "D %s", entries[idx].c_str());
  else
    snprintf(buf, sizeof(buf), "  %s", entries[idx].c_str());
  buf[20] = '\0';
  draw_str(0, y, buf, fg, sel ? SEL_BG : BLACK);
}

static void refreshList() {
  for (int r = 0; r < 7; r++)
    drawEntry(r + 1, scrollOffset + r);
}

static void browseDir(const String& path) {
  currentPath = path;
  entryCount = 0;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return;
  hasParent = (path != "/");
  if (hasParent) {
    entries[0] = "..";
    entryIsDir[0] = true;
    entryCount = 1;
  }
  File f;
  while ((f = dir.openNextFile()) && entryCount < MAX_ENTRIES) {
    String name = f.name();
    if (name == ".") { f.close(); continue; }
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    entries[entryCount] = name;
    entryIsDir[entryCount] = f.isDirectory();
    entryCount++;
    f.close();
  }
  dir.close();
  scrollOffset = 0;
  selection = 0;
}

static void drawPath() {
  clearRow(0);
  draw_str(0, 0, currentPath.c_str(), WHITE, BLACK);
}

static void drawPopup() {
  int pw = 140, ph = 56;
  int px = (160 - pw) / 2, py = (128 - ph) / 2;
  fill_rect(px, py, pw, ph, DKGRAY);
  fill_rect(px + 2, py + 2, pw - 4, ph - 4, BLACK);
  const char* items[3] = {"1. Delete", "2. Info", "3. Exit"};
  int left = px + 6;
  for (int i = 0; i < 3; i++) {
    int yy = py + 6 + i * 16;
    char buf[24];
    snprintf(buf, sizeof(buf), "%s%s", (i == popupSel) ? "*" : " ", items[i]);
    draw_str(left, yy, buf, (i == popupSel) ? WHITE : GRAY, BLACK);
  }
}

static void drawDeleteConfirm() {
  int pw = 140, ph = 40;
  int px = (160 - pw) / 2, py = (128 - ph) / 2;
  fill_rect(px, py, pw, ph, DKGRAY);
  fill_rect(px + 2, py + 2, pw - 4, ph - 4, BLACK);
  int left = px + 6;
  if (selection >= 0 && selection < entryCount && entries[selection] != "..") {
    char buf[32];
    snprintf(buf, sizeof(buf), "Delete %s?", entries[selection].c_str());
    draw_str(left, py + 6, buf, WHITE, BLACK);
    draw_str(left, py + 22, "A=Yes  B=No", GRAY, BLACK);
  }
}

static void drawFileDetails() {
  fill_screen(BLACK);
  if (selection >= 0 && selection < entryCount) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Name: %s", entries[selection].c_str());
    draw_str(0, 16, buf, WHITE, BLACK);
    snprintf(buf, sizeof(buf), "Type: %s", entryIsDir[selection] ? "Dir" : "File");
    draw_str(0, 40, buf, GRAY, BLACK);
    if (!entryIsDir[selection]) {
      String fullPath = currentPath;
      if (fullPath != "/") fullPath += "/";
      fullPath += entries[selection];
      File f = SD.open(fullPath);
      if (f) {
        snprintf(buf, sizeof(buf), "Size: %u bytes", (unsigned int)f.size());
        f.close();
      } else {
        snprintf(buf, sizeof(buf), "Size: N/A");
      }
      draw_str(0, 64, buf, GRAY, BLACK);
    }
  }
  draw_str_center(112, "Press any key", GRAY, BLACK);
}

static void deleteSelected() {
  if (selection < 0 || selection >= entryCount) return;
  if (entries[selection] == "..") return;
  String fullPath = currentPath;
  if (fullPath != "/") fullPath += "/";
  fullPath += entries[selection];
  if (entryIsDir[selection]) SD.rmdir(fullPath);
  else SD.remove(fullPath);
  browseDir(currentPath);
  drawPath();
  refreshList();
}

void sd_manager_init() {
  state = 0;
  popupSel = 0;
  scrollOffset = 0;
  selection = 0;
  memset(lastBtn, 0, sizeof(lastBtn));
  fill_screen(BLACK);
  draw_str_center(56, "SD Init...", WHITE, BLACK);
  spi.end();
  spi.begin(18, 19, 23, -1);
  delay(10);
  if (!SD.begin(22, spi, 4000000)) {
    fill_screen(BLACK);
    draw_str_center(48, "SD Card Init Fail!", WHITE, BLACK);
    draw_str_center(72, "Check SD card", GRAY, BLACK);
    draw_str_center(96, "Press B to return", GRAY, BLACK);
    memset(lastBtn, 0, sizeof(lastBtn));
    unsigned long timer = 0;
    while (true) {
      unsigned long n = millis();
      if (n - timer > 200) {
        if (digitalRead(12) == LOW) {
          show_menu();
          return;
        }
        timer = n;
      }
      delay(5);
    }
  }
  browseDir("/");
  drawPath();
  refreshList();
}

void sd_manager_loop() {
  unsigned long n = millis();
  if (n - lastInput < 200) return;
  bool up = btnPressed(0);
  bool dn = btnPressed(1);
  bool a  = btnPressed(4);
  bool b  = btnPressed(5);
  if (!up && !dn && !a && !b) return;
  lastInput = n;
  switch (state) {
    case 0:
      if (up && selection > 0) {
        selection--;
        if (selection < scrollOffset) scrollOffset--;
        drawPath();
        refreshList();
      } else if (dn && selection < entryCount - 1) {
        selection++;
        if (selection >= scrollOffset + 7) scrollOffset++;
        drawPath();
        refreshList();
      } else if (a && selection >= 0 && selection < entryCount) {
        if (entries[selection] == "..") {
          browseDir(getParentPath(currentPath));
          drawPath();
          refreshList();
        } else if (entryIsDir[selection]) {
          String newPath = currentPath;
          if (newPath != "/") newPath += "/";
          newPath += entries[selection];
          browseDir(newPath);
          drawPath();
          refreshList();
        }
      } else if (b) {
        state = 1;
        popupSel = 0;
        drawPopup();
      }
      break;
    case 1:
      if (up && popupSel > 0) {
        popupSel--;
        drawPopup();
      } else if (dn && popupSel < 2) {
        popupSel++;
        drawPopup();
      } else if (a) {
        switch (popupSel) {
          case 0:
            if (entries[selection] != "..") {
              state = 2;
              drawDeleteConfirm();
            } else {
              state = 0;
              drawPath();
              refreshList();
            }
            break;
          case 1:
            state = 3;
            drawFileDetails();
            break;
          case 2:
            show_menu();
            return;
        }
      } else if (b) {
        state = 0;
        drawPath();
        refreshList();
      }
      break;
    case 2:
      if (a) {
        deleteSelected();
        state = 0;
        drawPath();
        refreshList();
      } else if (b) {
        state = 0;
        drawPath();
        refreshList();
      }
      break;
    case 3:
      state = 0;
      drawPath();
      refreshList();
      break;
  }
}