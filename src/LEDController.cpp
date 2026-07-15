#include "LEDController.h"

LEDController::LEDController(int pin) 
    : _pin(pin), _state(LED_STATE_OFF), _lastToggle(0), _isOn(false),
      _slowInterval(1000), _fastInterval(200) {
}

void LEDController::begin() {
    if (_pin > 0) {
        pinMode(_pin, OUTPUT);
        digitalWrite(_pin, LOW);
        Serial.printf("[LED] 初始化完成 | 引脚: GPIO%d\n", _pin);
    }
}

void LEDController::update() {
    if (_pin <= 0) return;
    
    unsigned long now = millis();
    
    switch (_state) {
        case LED_STATE_OFF:
            if (_isOn) {
                digitalWrite(_pin, LOW);
                _isOn = false;
            }
            break;
            
        case LED_STATE_ON:
            if (!_isOn) {
                digitalWrite(_pin, HIGH);
                _isOn = true;
            }
            break;
            
        case LED_STATE_BLINK_SLOW:
            if (now - _lastToggle >= _slowInterval) {
                _lastToggle = now;
                _isOn = !_isOn;
                digitalWrite(_pin, _isOn ? HIGH : LOW);
            }
            break;
            
        case LED_STATE_BLINK_FAST:
            if (now - _lastToggle >= _fastInterval) {
                _lastToggle = now;
                _isOn = !_isOn;
                digitalWrite(_pin, _isOn ? HIGH : LOW);
            }
            break;
            
        case LED_STATE_BLINK_ONCE:
            if (now - _lastToggle >= 100) {
                digitalWrite(_pin, LOW);
                _isOn = false;
                _state = LED_STATE_OFF;
            }
            break;
    }
}

void LEDController::setState(LEDState state) {
    if (_pin <= 0) return;
    
    if (_state != state) {
        _state = state;
        _lastToggle = millis();
        
        if (state == LED_STATE_BLINK_ONCE) {
            digitalWrite(_pin, HIGH);
            _isOn = true;
        }
        
        Serial.printf("[LED] 状态变更: ");
        switch(state) {
            case LED_STATE_OFF: Serial.println("OFF"); break;
            case LED_STATE_ON: Serial.println("ON"); break;
            case LED_STATE_BLINK_SLOW: Serial.println("BLINK_SLOW"); break;
            case LED_STATE_BLINK_FAST: Serial.println("BLINK_FAST"); break;
            case LED_STATE_BLINK_ONCE: Serial.println("BLINK_ONCE"); break;
        }
    }
}

LEDState LEDController::getState() const {
    return _state;
}

void LEDController::setBlinkInterval(unsigned long slow_ms, unsigned long fast_ms) {
    _slowInterval = slow_ms;
    _fastInterval = fast_ms;
}