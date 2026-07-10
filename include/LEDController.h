#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include "config.h"

typedef enum {
    LED_STATE_OFF,
    LED_STATE_ON,
    LED_STATE_BLINK_SLOW,
    LED_STATE_BLINK_FAST,
    LED_STATE_BLINK_ONCE
} LEDState;

class LEDController {
public:
    LEDController(int pin = PIN_LED_D4);
    void begin();
    void update();
    void setState(LEDState state);
    LEDState getState() const;
    
    void setBlinkInterval(unsigned long slow_ms, unsigned long fast_ms);

private:
    int _pin;
    LEDState _state;
    unsigned long _lastToggle;
    bool _isOn;
    unsigned long _slowInterval;
    unsigned long _fastInterval;
};

#endif
