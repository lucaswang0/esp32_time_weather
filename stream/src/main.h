#pragma once
#include <stdint.h>

#define BLACK   0x0000
#define WHITE   0xFFFF
#define BLUE    0x001F
#define DARKBL  0x0044AA
#define GRAY    0x8410
#define DKGRAY  0x39E7
#define GREEN   0x07E0
#define SEL_BG  0x0044AA
#define BTN_BG  0x333333

#define WIFI_STATE_OFF 0
#define WIFI_STATE_AP  1
#define WIFI_STATE_STA 2

extern int wifi_state;

#define PIN_CS 5
#define PIN_DC 4
#define PIN_MOSI 23
#define PIN_SCLK 18
