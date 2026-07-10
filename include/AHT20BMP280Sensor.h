#ifndef AHT20_BMP280_SENSOR_H
#define AHT20_BMP280_SENSOR_H

#include <Arduino.h>

class AHT20BMP280Sensor {
public:
    AHT20BMP280Sensor(int sdaPin, int sclPin);
    bool begin();
    void update();

    float getTemperature() const;
    float getHumidity() const;
    float getPressure() const;
    // 返回 BMP280 die 内部温度（仅温度补偿用，不应作为环境温度使用，会比真实值高 0.5~2°C 因为 die 自加热）
    float getBmpTemperature() const;
    bool isValid() const;

    bool isAHT20Connected() const;
    bool isBMP280Connected() const;

private:
    int _sdaPin;
    int _sclPin;

    float _temperature;        // AHT20 环境温度（真实）
    float _bmpTemperature;     // BMP280 die 内部温度（自加热，不用作真实显示）
    float _humidity;           // AHT20 湿度
    float _pressure;           // BMP280 气压
    bool _valid;
    bool _aht20Connected;
    bool _bmp280Connected;

    unsigned long _lastUpdate;
    const unsigned long _updateInterval;

    // BMP280校准数据结构
    struct BMP280CalibData {
        uint16_t dig_T1;
        int16_t dig_T2;
        int16_t dig_T3;
        uint16_t dig_P1;
        int16_t dig_P2;
        int16_t dig_P3;
        int16_t dig_P4;
        int16_t dig_P5;
        int16_t dig_P6;
        int16_t dig_P7;
        int16_t dig_P8;
        int16_t dig_P9;
    } _calib;

    bool readAHT20();
    bool readBMP280();

    // ⭐ 这三个函数声明必须存在 ⭐
    bool readCalibrationData();
    int32_t compensateTemperature(int32_t rawTemp);
    float compensatePressure(int32_t rawPress, int32_t t_fine);

    uint8_t readBMP280Register(uint8_t reg);
    void writeBMP280Register(uint8_t reg, uint8_t value);
};

#endif
