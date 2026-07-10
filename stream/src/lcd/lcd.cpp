#include <Arduino.h>
#include <SPI.h>
#include "../main.h"
#include "lcd.h"
#include "../font_8x16.h"

SPIClass spi(VSPI);

static void lcd_cmd(uint8_t c) {
  digitalWrite(PIN_DC, LOW); digitalWrite(PIN_CS, LOW); spi.transfer(c); digitalWrite(PIN_CS, HIGH);
}
static void lcd_dat_start() { digitalWrite(PIN_DC, HIGH); digitalWrite(PIN_CS, LOW); }
static void lcd_dat_end() { digitalWrite(PIN_CS, HIGH); }
static void lcd_dat(uint8_t d) { lcd_dat_start(); spi.transfer(d); lcd_dat_end(); }

void lcd_init() {
  pinMode(PIN_CS, OUTPUT); digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT); digitalWrite(PIN_DC, HIGH);
  spi.begin(PIN_SCLK, 19, PIN_MOSI, PIN_CS); spi.setFrequency(40000000);
  delay(50);
  lcd_cmd(0x01); delay(5);
  lcd_cmd(0x11); delay(120);
  lcd_cmd(0xB1); lcd_dat(0x05); lcd_dat(0x3A); lcd_dat(0x3A);
  lcd_cmd(0xB2); lcd_dat(0x05); lcd_dat(0x3A); lcd_dat(0x3A);
  lcd_cmd(0xB3); lcd_dat(0x05); lcd_dat(0x3A); lcd_dat(0x3A); lcd_dat(0x05); lcd_dat(0x3A); lcd_dat(0x3A);
  lcd_cmd(0xB4); lcd_dat(0x03);
  lcd_cmd(0xC0); lcd_dat(0x62); lcd_dat(0x02); lcd_dat(0x04);
  lcd_cmd(0xC1); lcd_dat(0x00);
  lcd_cmd(0xC2); lcd_dat(0x0C); lcd_dat(0x00);
  lcd_cmd(0xC3); lcd_dat(0x8D); lcd_dat(0x6A);
  lcd_cmd(0xC4); lcd_dat(0x8D); lcd_dat(0xEE);
  lcd_cmd(0xC5); lcd_dat(0x0E);
  lcd_cmd(0x36); lcd_dat(0x40);
  lcd_cmd(0x3A); lcd_dat(0x05);
  lcd_cmd(0x29); delay(10);
}

#define COL_OFF 0
#define ROW_OFF 0

static void lcd_set_win(int x1, int y1, int x2, int y2) {
  lcd_cmd(0x2A); lcd_dat_start(); spi.transfer((y1+ROW_OFF)>>8); spi.transfer((y1+ROW_OFF)&0xFF); spi.transfer((y2+ROW_OFF)>>8); spi.transfer((y2+ROW_OFF)&0xFF); lcd_dat_end();
  lcd_cmd(0x2B); lcd_dat_start(); spi.transfer((x1+COL_OFF)>>8); spi.transfer((x1+COL_OFF)&0xFF); spi.transfer((x2+COL_OFF)>>8); spi.transfer((x2+COL_OFF)&0xFF); lcd_dat_end();
  lcd_cmd(0x2C); lcd_dat_start();
}

void fill_screen(uint16_t c) {
  lcd_cmd(0x2A); lcd_dat_start(); spi.transfer(0>>8); spi.transfer(0&0xFF); spi.transfer(127>>8); spi.transfer(127&0xFF); lcd_dat_end();
  lcd_cmd(0x2B); lcd_dat_start(); spi.transfer(0>>8); spi.transfer(0&0xFF); spi.transfer(159>>8); spi.transfer(159&0xFF); lcd_dat_end();
  lcd_cmd(0x2C); lcd_dat_start();
  for (int i = 0; i < 20480; i++) { spi.transfer(c>>8); spi.transfer(c&0xFF); }
  lcd_dat_end();
}

void fill_rect(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
  if (x + w > 160) w = 160 - x; if (y + h > 128) h = 128 - y;
  if (w <= 0 || h <= 0) return;
  lcd_set_win(x, y, x + w - 1, y + h - 1);
  for (int i = 0; i < w * h; i++) { spi.transfer(c>>8); spi.transfer(c&0xFF); }
  lcd_dat_end();
}

void draw_pixel(int x, int y, uint16_t c) {
  if (x < 0 || x >= 160 || y < 0 || y >= 128) return;
  lcd_set_win(x, y, x, y);
  spi.transfer(c>>8); spi.transfer(c&0xFF);
  lcd_dat_end();
}

void draw_char(int x, int y, unsigned char ch, uint16_t fg, uint16_t bg) {
  if (ch < 0x20 || ch > 0x7E) return;
  int idx = ch - 0x20;
  for (int row = 0; row < 16; row++) {
    uint8_t bits = font8x16[idx][row];
    int yy = y + row;
    if (yy < 0 || yy >= 128) continue;
    lcd_set_win(x, yy, x + 7, yy);
    for (int col = 0; col < 8; col++) {
      uint16_t c = (bits & (1 << (7 - col))) ? fg : bg;
      spi.transfer(c>>8); spi.transfer(c&0xFF);
    }
    lcd_dat_end();
  }
}

void draw_str(int x, int y, const char* s, uint16_t fg, uint16_t bg) {
  while (*s) {
    if (x + 8 > 160) break;
    draw_char(x, y, *s, fg, bg);
    x += 8; s++;
  }
}

void draw_str_center(int y, const char* s, uint16_t fg, uint16_t bg) {
  int len = 0; const char* p = s; while (*p) { len++; p++; }
  int x = (160 - len * 8) / 2;
  if (x < 0) x = 0;
  draw_str(x, y, s, fg, bg);
}

void draw_menu_item(int x, int y, int num, const char* label, bool sel) {
  char buf[32];
  sprintf(buf, "%s%d. %s", sel ? "*" : " ", num, label);
  draw_str(x, y, buf, sel ? WHITE : GRAY, BLACK);
}

void blit_area(int x, int y, const uint16_t* data, int w, int h) {
  lcd_set_win(x, y, x + w - 1, y + h - 1);
  for (int i = 0; i < w * h; i++) { spi.transfer(data[i]>>8); spi.transfer(data[i]&0xFF); }
  lcd_dat_end();
}

void draw_wifi_icon(int x, int y, uint16_t color) {
  fill_rect(x, y, 2, 2, color);
}
