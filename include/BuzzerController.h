#ifndef BUZZER_CONTROLLER_H
#define BUZZER_CONTROLLER_H

#include <Arduino.h>

class BuzzerController {
public:
    BuzzerController(int pin);
    
    void begin();
    void update();
    void radioChime();
    void startupChime();
    
private:
    int _pin;
    void startSound(int freq);
    void stopSound();
};

#endif