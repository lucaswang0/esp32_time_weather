#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <BLEHIDDevice.h>
#include "../main.h"
#include "../lcd/lcd.h"
#include "../input/input.h"
#include "bt_gamepad.h"
#include "../menu/menu.h"

static BLEHIDDevice* hid = NULL;
static BLECharacteristic* input = NULL;
static volatile bool deviceConnected = false;

static int btn_mode[6] = {0};
static unsigned long long_press[6] = {0};
static bool last_btn[6] = {0};
static char btn_display[64] = "";
static bool bt_connected = false;

#define BIT_A     0
#define BIT_B     1
#define BIT_UP    2
#define BIT_DOWN  3
#define BIT_LEFT  4
#define BIT_RIGHT 5

static const uint8_t hidReportDescriptor[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
  0x85, 0x01,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
  0x15, 0x00, 0x25, 0x01,
  0x75, 0x01, 0x95, 0x08,
  0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x06, 0x75, 0x08,
  0x15, 0x00, 0x26, 0xFF, 0x00,
  0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00,
  0x81, 0x00,
  0xC0
};

class BTServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer*) { deviceConnected = true; }
  void onDisconnect(BLEServer* s) { deviceConnected = false; s->getAdvertising()->start(); }
};

void bt_gamepad_init() {
  fill_screen(BLACK);
  draw_str_center(4, "BT Gamepad", WHITE, BLACK);
  draw_str_center(22, "A:A  B:B", GRAY, BLACK);
  draw_str_center(38, "Status: Waiting...", GRAY, BLACK);
  btn_display[0] = 0;

  for (int i = 0; i < 6; i++) { last_btn[i] = false; long_press[i] = 0; }
  btn_mode[4] = 0; btn_mode[5] = 0;

  BLEDevice::init("Xueersi Gamepad");
  BLEServer* srv = BLEDevice::createServer();
  srv->setCallbacks(new BTServerCB());
  hid = new BLEHIDDevice(srv);
  input = hid->inputReport(1);
  hid->manufacturer()->setValue("Espressif");
  hid->pnp(0x02, 0x1234, 0x5678, 0x0110);
  hid->hidInfo(0x00, 0x01);
  hid->reportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));
  hid->startServices();

  BLEAdvertising* adv = srv->getAdvertising();
  adv->setAppearance(0x03C1);
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->start();
}

void bt_gamepad_loop() {
  if (deviceConnected != bt_connected) {
    bt_connected = deviceConnected;
    fill_rect(0, 38, 160, 16, BLACK);
    draw_str_center(38, deviceConnected ? "Status: Running" : "Status: Waiting...", GRAY, BLACK);
    if (!deviceConnected) { fill_rect(0, 96, 160, 32, BLACK); btn_display[0] = 0; }
  }

  int combo = 0;
  for (int i = 0; i < 6; i++) {
    bool curr = (digitalRead(btn_pins[i]) == LOW);
    if (curr && !last_btn[i]) long_press[i] = millis();
    if (!curr) long_press[i] = 0;
    last_btn[i] = curr;
    if (curr) combo++;
  }

  if (combo >= 2) {
    bool both_sides = (digitalRead(27) == LOW && digitalRead(35) == LOW);
    if (both_sides && long_press[2] > 0 && millis() - long_press[2] >= 1000) {
      btn_mode[4] = !btn_mode[4]; btn_mode[5] = !btn_mode[5];
      long_press[2] = 0; long_press[3] = 0;
      fill_rect(0, 22, 160, 16, BLACK);
      draw_str_center(22, btn_mode[4] ? "A:Space B:Enter" : "A:A  B:B", GRAY, BLACK);
      return;
    }
  }

  uint8_t report[8] = {0};
  char buf[64] = ""; int idx = 2;
  static const uint8_t kc[6] = {0x52, 0x51, 0x50, 0x4F, 0x04, 0x05};
  static const uint8_t kc_alt[2] = {0x2C, 0x28};
  static const char* btn_labels[6] = {"UP", "DOWN", "LEFT", "RIGHT", "A", "B"};
  static const char* btn_labels_alt[6] = {"UP", "DOWN", "LEFT", "RIGHT", "Space", "Enter"};

  for (int i = 0; i < 6; i++) {
    if (!last_btn[i]) continue;
    const char* name = (i >= 4 && btn_mode[i]) ? btn_labels_alt[i] : btn_labels[i];
    uint8_t key = (i >= 4 && btn_mode[i]) ? kc_alt[i-4] : kc[i];
    if (idx < 8) report[idx++] = key;
    if (buf[0]) strcat(buf, " + ");
    strcat(buf, name);
  }

  static uint8_t prevReport[8] = {0};
  if (memcmp(report, prevReport, 8) != 0) {
    if (deviceConnected) {
      bool swap = false;
      for (int i = 0; i < 8; i++) {
        if (report[i] != prevReport[i] && report[i] != 0 && prevReport[i] != 0) { swap = true; break; }
      }
      if (swap) {
        uint8_t empty[8] = {0};
        input->setValue(empty, 8); input->notify();
        delay(5);
      }
      input->setValue(report, 8); input->notify();
    }
    memcpy(prevReport, report, 8);
  }

  if (strcmp(buf, btn_display) != 0) {
    strcpy(btn_display, buf);
    fill_rect(0, 96, 160, 32, BLACK);
    if (buf[0]) draw_str_center(100, buf, WHITE, BLACK);
  }

  if (!deviceConnected && digitalRead(12) == LOW) {
    BLEDevice::deinit(false);
    show_menu();
    return;
  }
}

bool bt_gamepad_is_connected() {
  return deviceConnected;
}