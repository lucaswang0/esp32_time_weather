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
        ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);
        
        Serial.printf("[Buzzer] 初始化完成 | 引脚: GPIO%d, LEDC通道: %d, 低电平触发\n", _pin, BUZZER_LEDC_CHANNEL);
    }
}

void BuzzerController::stopSound() {
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);
}

void BuzzerController::startSound(int freq) {
    if (_pin <= 0) return;
    
    stopSound();
    
    ledcSetup(BUZZER_LEDC_CHANNEL, freq, BUZZER_RESOLUTION);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY / 2);
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
        ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);
        if (i < 4) {
            delay(800);
        } else {
            delay(500);
        }
    }
    
    ledcWriteTone(BUZZER_LEDC_CHANNEL, 1600);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY / 2);
    delay(300);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);
    
    Serial.println("[Buzzer] Radio chime finished");
}


/**
 * @brief 播放设备启动自检声
 * @param durationMs 每个音调的持续时间（毫秒），默认 80ms
 * @param intervalMs 两个音调之间的间隔（毫秒），默认 100ms
 */
void BuzzerController::startupChime() {
    if (_pin <= 0) return;
    
    ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_BASE_FREQ, BUZZER_RESOLUTION);
    ledcAttachPin(_pin, BUZZER_LEDC_CHANNEL);
    
    // 固定的自检声逻辑（不带参数）
    for (int freq = 600; freq <= 1200; freq += 50) {
        ledcWriteTone(BUZZER_LEDC_CHANNEL, freq);
        ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY / 2);
        delay(8);
    }
    
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);
    delay(100);
    
    ledcWriteTone(BUZZER_LEDC_CHANNEL, 2000);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY / 2);
    delay(80);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);
}

/**
 * @brief 短按反馈音：清脆的"嘀"
 */
void BuzzerController::touchFeedbackShort() {
    if (_pin <= 0) return;
    
    ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_BASE_FREQ, BUZZER_RESOLUTION);
    ledcAttachPin(_pin, BUZZER_LEDC_CHANNEL);
    
    ledcWriteTone(BUZZER_LEDC_CHANNEL, 2500);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY / 2);
    delay(50);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);
}

/**
 * @brief 双击反馈音：两声快速的"嘀嘀"
 */
void BuzzerController::touchFeedbackDouble() {
    if (_pin <= 0) return;
    
    ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_BASE_FREQ, BUZZER_RESOLUTION);
    ledcAttachPin(_pin, BUZZER_LEDC_CHANNEL);
    
    // 第一声
    ledcWriteTone(BUZZER_LEDC_CHANNEL, 2500);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY / 2);
    delay(30);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);
    
    delay(50);  // 两音间隔
    
    // 第二声
    ledcWriteTone(BUZZER_LEDC_CHANNEL, 2500);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY / 2);
    delay(30);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);
}

/**
 * @brief 长按反馈音：低沉的"嘟——"
 */
void BuzzerController::touchFeedbackLong() {
    if (_pin <= 0) return;
    
    ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_BASE_FREQ, BUZZER_RESOLUTION);
    ledcAttachPin(_pin, BUZZER_LEDC_CHANNEL);
    
    // 低频 + 稍长时长，营造"沉重"感
    ledcWriteTone(BUZZER_LEDC_CHANNEL, 800);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY / 2);
    delay(200);
    ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);
}


/**
 * @brief 播放 RTTTL 格式的旋律
 * @param song RTTTL 格式的字符串
 */
void BuzzerController::playRTTTL(const char* song) {
    if (_pin <= 0) return;
    
    ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_BASE_FREQ, BUZZER_RESOLUTION);
    ledcAttachPin(_pin, BUZZER_LEDC_CHANNEL);
    
    // 解析设置部分
    const char* p = song;
    
    // 跳过名称（第一个 ':' 之前的部分）
    while (*p != ':' && *p != '\0') p++;
    if (*p == '\0') return;
    p++; // 跳过 ':'
    
    // 解析设置 (d, o, b)
    int defaultDuration = 4;   // 默认四分音符
    int defaultOctave = 5;     // 默认八度 5
    int tempo = 120;           // 默认速度 120 BPM
    
    while (*p != ':' && *p != '\0') {
        if (*p == 'd') {
            p++;
            if (*p == '=') p++;
            defaultDuration = atoi(p);
            while (*p >= '0' && *p <= '9') p++;
        } else if (*p == 'o') {
            p++;
            if (*p == '=') p++;
            defaultOctave = atoi(p);
            while (*p >= '0' && *p <= '9') p++;
        } else if (*p == 'b') {
            p++;
            if (*p == '=') p++;
            tempo = atoi(p);
            while (*p >= '0' && *p <= '9') p++;
        } else {
            p++;
        }
        if (*p == ',') p++;
    }
    if (*p == '\0') return;
    p++; // 跳过 ':'
    
    // 计算每个四分音符的毫秒数
    int noteDurationMs = 60000 / tempo;
    
    // 解析并播放音符序列
    while (*p != '\0') {
        // 跳过空格
        while (*p == ' ') p++;
        if (*p == '\0') break;
        
        // 解析音符
        int note = -1;
        int octave = defaultOctave;
        int duration = defaultDuration;
        bool isSharp = false;
        
        // 读取音符字母 (c, d, e, f, g, a, b)
        char noteChar = *p;
        switch (noteChar) {
            case 'c': note = 0; break;
            case 'd': note = 1; break;
            case 'e': note = 2; break;
            case 'f': note = 3; break;
            case 'g': note = 4; break;
            case 'a': note = 5; break;
            case 'b': note = 6; break;
            case 'p': note = -1; break;  // 休止符
            default: break;
        }
        if (noteChar == '\0') break;
        p++;
        
        // 检查升号 #
        if (*p == '#') {
            isSharp = true;
            p++;
        }
        
        // 检查八度
        if (*p >= '0' && *p <= '9') {
            octave = *p - '0';
            p++;
        }
        
        // 检查时长
        if (*p >= '0' && *p <= '9') {
            duration = atoi(p);
            while (*p >= '0' && *p <= '9') p++;
        }
        
        // 检查附点 .
        bool isDotted = false;
        if (*p == '.') {
            isDotted = true;
            p++;
        }
        
        // 计算实际频率
        int freq = 0;
        if (note >= 0) {
            // 音符频率表 (C4 = 261.63Hz)
            const int noteFreq[] = {261, 293, 329, 349, 392, 440, 493};
            // 每升高一个八度，频率翻倍
            int octaveShift = octave - 4;
            freq = noteFreq[note] * (1 << (octaveShift > 0 ? octaveShift : 0)) / (1 << (octaveShift < 0 ? -octaveShift : 0));
            
            // 升号：频率乘以 2^(1/12) ≈ 1.05946
            if (isSharp) {
                freq = freq * 106 / 100;
            }
        }
        
        // 计算音符时长
        int playTime = noteDurationMs / duration;
        if (isDotted) {
            playTime = playTime * 3 / 2;  // 附点 = 1.5 倍
        }
        
        // 播放音符
        if (freq > 0) {
            ledcWriteTone(BUZZER_LEDC_CHANNEL, freq);
            ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY / 2);
        } else {
            ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);  // 休止符静音
        }
        
        delay(playTime);
        ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_MAX_DUTY);
        delay(playTime / 8);  // 音符间短暂停顿
        
        // 跳到下一个音符
        while (*p == ',' || *p == ' ') p++;
    }
}

/**
 * @brief 通过 RTTTL 播放节日旋律
 */
void BuzzerController::playFestivalByRTTTL(int festivalType) {
    const char* melody = nullptr;
    
    switch (festivalType) {
        case FESTIVAL_CHRISTMAS:
            // 《铃儿响叮当》
            melody = "007:o=5,d=4,b=320,b=320:c,8d,8d,d,2d,c,c,c,c,8d#,8d#,2d#,d,d,d,c,8d,8d,d,2d,c,c,c,c,8d#,8d#,d#,2d#,d,c#,c,c6,1b.,g,f,1g.";
            break;
            
        case FESTIVAL_BIRTHDAY:
            // 《祝你生日快乐》
            melody = "Birthday:d=4,o=5,b=100:g,g,a,g,c6,b, g,g,a,g,d6,c6, g,g,g6,e6,c6,b,a, f,f,e6,c6,d6,c6";
            break;
            
        case FESTIVAL_NEW_YEAR:
            // 《恭喜发财》简化片段
            melody = "New Year:o=5,d=8,b=125,b=125:a4,4d.,d,4d,4f#,4e.,d,4e,f#,e,4d.,d,4f#,4a,2b.,4b,4a.,f#,4f#,4d,4e.,d,4e,f#,e,4d.,b4,4b4,4a4,2d,16p";
            break;
            
        default:
            touchFeedbackShort();
            return;
    }
    
    if (melody) {
        playRTTTL(melody);
    }
}