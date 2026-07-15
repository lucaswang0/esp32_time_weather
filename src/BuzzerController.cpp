#include "BuzzerController.h"
#include "config.h"

#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL
#define BUZZER_BASE_FREQ    LEDC_BASE_FREQ
#define BUZZER_RESOLUTION   LEDC_TIMER_BIT
#define BUZZER_MAX_DUTY     ((1 << BUZZER_RESOLUTION) - 1)

BuzzerController::BuzzerController(int pin) : _pin(pin) {
}

void BuzzerController::begin() {
    if (_pin > 0) {
        pinMode(_pin, OUTPUT);
        digitalWrite(_pin, HIGH);
        
        ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_BASE_FREQ, BUZZER_RESOLUTION);
        ledcAttachPin(_pin, BUZZER_LEDC_CHANNEL);
        ledcWrite(BUZZER_LEDC_CHANNEL, 0);
        
        Serial.printf("[Buzzer] 初始化完成 | 引脚: GPIO%d, LEDC通道: %d\n", _pin, BUZZER_LEDC_CHANNEL);
    }
}

void BuzzerController::stopSound() {
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
}

void BuzzerController::startSound(int freq) {
    if (_pin <= 0) return;
    
    stopSound();
    
    ledcSetup(BUZZER_LEDC_CHANNEL, freq, BUZZER_RESOLUTION);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);
}

void BuzzerController::update() {
}

void BuzzerController::radioChime() {
    if (_pin <= 0) return;
    
    stopSound();
    
    ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_BASE_FREQ, BUZZER_RESOLUTION);
    ledcAttachPin(_pin, BUZZER_LEDC_CHANNEL);
    
    for (int i = 0; i < 5; i++) {
        ledcWriteTone(BUZZER_LEDC_CHANNEL, 800);
        ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY / 2);
        delay(200);
        ledcWrite(BUZZER_LEDC_CHANNEL, 0);
        if (i < 4) {
            delay(800);
        } else {
            delay(500);
        }
    }
    
    ledcWriteTone(BUZZER_LEDC_CHANNEL, 1600);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY / 2);
    delay(300);
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    
    Serial.println("[Buzzer] Radio chime finished");
}