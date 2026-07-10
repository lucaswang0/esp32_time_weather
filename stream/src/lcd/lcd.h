#pragma once
#include <stdint.h>
#include <SPI.h>

extern SPIClass spi;

void lcd_init();
void fill_screen(uint16_t c);
void fill_rect(int x, int y, int w, int h, uint16_t c);
void draw_pixel(int x, int y, uint16_t c);
void draw_char(int x, int y, unsigned char ch, uint16_t fg, uint16_t bg);
void draw_str(int x, int y, const char* s, uint16_t fg, uint16_t bg);
void draw_str_center(int y, const char* s, uint16_t fg, uint16_t bg);
void draw_menu_item(int x, int y, int num, const char* label, bool sel);
void blit_area(int x, int y, const uint16_t* data, int w, int h);
void draw_wifi_icon(int x, int y, uint16_t color);
