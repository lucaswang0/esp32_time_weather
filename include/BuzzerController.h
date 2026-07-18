#ifndef BUZZER_CONTROLLER_H
#define BUZZER_CONTROLLER_H

#define FESTIVAL_CHRISTMAS  1
#define FESTIVAL_BIRTHDAY   2
#define FESTIVAL_NEW_YEAR   3

#include <Arduino.h>

class BuzzerController {
public:
    BuzzerController(int pin);
    
    void begin();
    void update();
    void radioChime();
    void startupChime();
    void touchFeedbackShort();      // 短按：清脆嘀
    void touchFeedbackDouble();     // 双击：嘀嘀
    void touchFeedbackLong();       // 长按：嘟——
    void playRTTTL(const char* song);
    void playFestivalByRTTTL(int festivalType);

private:
    int _pin;
    void startSound();
    void startSound(int freq);
    void stopSound();
};

#endif