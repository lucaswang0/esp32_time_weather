#include <Arduino.h>
#include "input.h"

const int btn_pins[6] = {2, 13, 27, 35, 34, 12};
static bool lastBtn[6] = {0};

void input_update() {
  for (int i = 0; i < 6; i++) {
    lastBtn[i] = (digitalRead(btn_pins[i]) == LOW);
  }
}

bool btnPressed(int idx) {
  bool curr = (digitalRead(btn_pins[idx]) == LOW);
  bool ret = curr && !lastBtn[idx];
  lastBtn[idx] = curr;
  return ret;
}
