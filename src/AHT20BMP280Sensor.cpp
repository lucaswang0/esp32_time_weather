#include "AHT20BMP280Sensor.h"
#include <Wire.h>

#define AHT20_ADDR 0x38
#define BMP280_ADDR 0x77  // 确认你的传感器地址，常见是0x76或0x77

#define AHT20_CMD_INIT 0xBE
#define AHT20_CMD_MEASURE 0xAC
#define AHT20_CMD_RESET 0xBA

#define BMP280_REG_ID 0xD0
#define BMP280_REG_CTRL_MEAS 0xF4
#define BMP280_REG_CONFIG 0xF5
#define BMP280_REG_PRESS 0xF7
#define BMP280_REG_TEMP 0xFA

// BMP280校准参数寄存器
#define BMP280_REG_CALIB 0x88

AHT20BMP280Sensor::AHT20BMP280Sensor(int sdaPin, int sclPin)
    : _sdaPin(sdaPin), _sclPin(sclPin),
      _temperature(0), _humidity(0), _pressure(0),
      _valid(false), _aht20Connected(false), _bmp280Connected(false),
      _lastUpdate(0), _updateInterval(2000) {}

bool AHT20BMP280Sensor::begin() {
    Wire.begin(_sdaPin, _sclPin);
    Wire.setClock(400000);

    delay(100);

    // === 初始化 AHT20 ===
    Wire.beginTransmission(AHT20_ADDR);
    Wire.write(AHT20_CMD_INIT);
    Wire.write(0x08);
    Wire.write(0x00);
    uint8_t aht20Status = Wire.endTransmission();

    if (aht20Status == 0) {
        delay(10);
        Wire.beginTransmission(AHT20_ADDR);
        Wire.write(0x71);
        if (Wire.endTransmission() == 0) {
            Wire.requestFrom(AHT20_ADDR, 1);
            if (Wire.available()) {
                uint8_t status = Wire.read();
                if ((status & 0x18) == 0x18) {
                    _aht20Connected = true;
                    Serial.printf("[AHT20+BMP280] AHT20 初始化成功 | SDA: GPIO%d | SCL: GPIO%d\n", _sdaPin, _sclPin);
                }
            }
        }
    }

    // === 初始化 BMP280 ===
    uint8_t bmp280Id = readBMP280Register(BMP280_REG_ID);
    if (bmp280Id == 0x58 || bmp280Id == 0x60) {  // BMP280 ID: 0x58 (BMP280) 或 0x60 (BME280)
        // 读取校准参数
        if (readCalibrationData()) {
            // 设置为正常模式，16x过采样
            writeBMP280Register(BMP280_REG_CTRL_MEAS, 0b00110111);  // 温度 x2, 压力 x16, 正常模式
            writeBMP280Register(BMP280_REG_CONFIG, 0b00000000);
            _bmp280Connected = true;
            Serial.printf("[AHT20+BMP280] BMP280 初始化成功 | ID: 0x%02X | 地址: 0x%02X\n", bmp280Id, BMP280_ADDR);
        } else {
            Serial.printf("[AHT20+BMP280] BMP280 校准数据读取失败\n");
        }
    } else {
        Serial.printf("[AHT20+BMP280] BMP280 未检测到 | 读取ID: 0x%02X\n", bmp280Id);
    }

    return _aht20Connected || _bmp280Connected;
}

bool AHT20BMP280Sensor::readCalibrationData() {
    Wire.beginTransmission(BMP280_ADDR);
    Wire.write(BMP280_REG_CALIB);
    if (Wire.endTransmission() != 0) return false;

    Wire.requestFrom(BMP280_ADDR, 24);
    if (Wire.available() < 24) return false;

    _calib.dig_T1 = Wire.read() | (Wire.read() << 8);
    _calib.dig_T2 = Wire.read() | (Wire.read() << 8);
    _calib.dig_T3 = Wire.read() | (Wire.read() << 8);
    _calib.dig_P1 = Wire.read() | (Wire.read() << 8);
    _calib.dig_P2 = Wire.read() | (Wire.read() << 8);
    _calib.dig_P3 = Wire.read() | (Wire.read() << 8);
    _calib.dig_P4 = Wire.read() | (Wire.read() << 8);
    _calib.dig_P5 = Wire.read() | (Wire.read() << 8);
    _calib.dig_P6 = Wire.read() | (Wire.read() << 8);
    _calib.dig_P7 = Wire.read() | (Wire.read() << 8);
    _calib.dig_P8 = Wire.read() | (Wire.read() << 8);
    _calib.dig_P9 = Wire.read() | (Wire.read() << 8);

    Serial.printf("[AHT20+BMP280] BMP280 校准数据读取成功 | T1:%u T2:%d T3:%d\n",
                  _calib.dig_T1, _calib.dig_T2, _calib.dig_T3);
    return true;
}

// BMP280温度补偿函数
int32_t AHT20BMP280Sensor::compensateTemperature(int32_t rawTemp) {
    int32_t var1, var2;
    int32_t t_fine;

    var1 = ((((rawTemp >> 3) - ((int32_t)_calib.dig_T1 << 1))) * ((int32_t)_calib.dig_T2)) >> 11;
    var2 = (((((rawTemp >> 4) - ((int32_t)_calib.dig_T1)) * ((rawTemp >> 4) - ((int32_t)_calib.dig_T1))) >> 12) * ((int32_t)_calib.dig_T3)) >> 14;
    t_fine = var1 + var2;

    return t_fine;
}

// BMP280压力补偿函数
float AHT20BMP280Sensor::compensatePressure(int32_t rawPress, int32_t t_fine) {
    int64_t var1, var2, p;

    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)_calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)_calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)_calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)_calib.dig_P3) >> 8) + ((var1 * (int64_t)_calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)_calib.dig_P1) >> 33;

    if (var1 == 0) {
        return 0;  // 避免除零
    }

    p = 1048576 - rawPress;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)_calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)_calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)_calib.dig_P7) << 4);

    return (float)p / 25600.0;  // 转换为 hPa
}

void AHT20BMP280Sensor::update() {
    unsigned long currentMillis = millis();
    if (currentMillis - _lastUpdate >= _updateInterval) {
        _lastUpdate = currentMillis;

        if (_aht20Connected) {
            readAHT20();
        }

        if (_bmp280Connected) {
            readBMP280();
        }

        _valid = true;
    }
}

float AHT20BMP280Sensor::getTemperature() const {
    return _temperature;
}

float AHT20BMP280Sensor::getBmpTemperature() const {
    return _bmpTemperature;
}

float AHT20BMP280Sensor::getHumidity() const {
    return _humidity;
}

float AHT20BMP280Sensor::getPressure() const {
    return _pressure;
}

bool AHT20BMP280Sensor::isValid() const {
    return _valid;
}

bool AHT20BMP280Sensor::isAHT20Connected() const {
    return _aht20Connected;
}

bool AHT20BMP280Sensor::isBMP280Connected() const {
    return _bmp280Connected;
}

bool AHT20BMP280Sensor::readAHT20() {
    Wire.beginTransmission(AHT20_ADDR);
    Wire.write(AHT20_CMD_MEASURE);
    Wire.write(0x33);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    delay(80);

    Wire.beginTransmission(AHT20_ADDR);
    Wire.write(0x71);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    Wire.requestFrom(AHT20_ADDR, 6);
    if (Wire.available() != 6) {
        return false;
    }

    uint8_t data[6];
    for (int i = 0; i < 6; i++) {
        data[i] = Wire.read();
    }

    if ((data[0] & 0x80) == 0) {
        uint32_t rawHumidity = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | ((data[3] >> 4) & 0x0F);
        uint32_t rawTemperature = ((uint32_t)(data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

        _humidity = ((float)rawHumidity / 1048576.0) * 100.0;
        _temperature = ((float)rawTemperature / 1048576.0) * 200.0 - 50.0;

        Serial.printf("[AHT20+BMP280] AHT20 读取成功 | 温度: %.2f°C | 湿度: %.2f%%\n", _temperature, _humidity);
        return true;
    }

    return false;
}

bool AHT20BMP280Sensor::readBMP280() {
    // 读取原始温度数据
    uint8_t tempData[3];
    tempData[0] = readBMP280Register(BMP280_REG_TEMP);
    tempData[1] = readBMP280Register(BMP280_REG_TEMP + 1);
    tempData[2] = readBMP280Register(BMP280_REG_TEMP + 2);

    // 读取原始压力数据
    uint8_t pressData[3];
    pressData[0] = readBMP280Register(BMP280_REG_PRESS);
    pressData[1] = readBMP280Register(BMP280_REG_PRESS + 1);
    pressData[2] = readBMP280Register(BMP280_REG_PRESS + 2);

    // 组合24-bit原始值（右对齐）
    int32_t rawTemp = ((int32_t)tempData[0] << 12) |
                     ((int32_t)tempData[1] << 4) |
                     ((tempData[2] >> 4) & 0x0F);

    int32_t rawPress = ((int32_t)pressData[0] << 12) |
                      ((int32_t)pressData[1] << 4) |
                      ((pressData[2] >> 4) & 0x0F);

    // 温度补偿
    int32_t t_fine = compensateTemperature(rawTemp);
    float temp = (t_fine * 5.0 + 128.0) / 25600.0;  // 转换为°C

    // 压力补偿
    float press = compensatePressure(rawPress, t_fine);

    // 更新成员变量
    _bmpTemperature = temp;  // BMP280 内部温度，单独保存，避免覆盖 AHT20 的真实环境温度
    _pressure = press;

    Serial.printf("[AHT20+BMP280] BMP280 读取成功 | 温度: %.2f°C | 气压: %.1f hPa (原始温度: %d, 压力: %d)\n",
                  temp, press, rawTemp, rawPress);
    return true;
}

uint8_t AHT20BMP280Sensor::readBMP280Register(uint8_t reg) {
    Wire.beginTransmission(BMP280_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) {
        return 0xFF;
    }

    Wire.requestFrom(BMP280_ADDR, 1);
    if (Wire.available()) {
        return Wire.read();
    }
    return 0xFF;
}

void AHT20BMP280Sensor::writeBMP280Register(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(BMP280_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
    delay(10);
}
