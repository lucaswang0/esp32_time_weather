#pragma once
#include <Arduino.h>

extern const int btn_pins[6];

bool btnPressed(int idx);
void input_update();
