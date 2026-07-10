#ifndef WEATHER_MANAGER_H
#define WEATHER_MANAGER_H

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include "WiFiManager.h"
#include "config.h"

// 城市信息结构体
struct CityInfo {
    String name;      // 城市名称
    String adm1;      // 省级行政区
    String adm2;      // 市级行政区
    String country;   // 国家
    String lat;       // 纬度
    String lon;       // 经度
};

// 天气预报结构体
struct DailyForecast {
    String date;      // 日期
    String textDay;   // 白天天气
    String tempMin;   // 最低温度
    String tempMax;   // 最高温度
    String humidity;  // 湿度
    String windDir;   // 风向
    String windScale; // 风力等级
    String sunrise;   // 日出时间
    String sunset;    // 日落时间
};

class WeatherManager {
public:
    WeatherManager(WiFiManager& wifiManager);
    
    // 获取当前天气
    bool fetchCurrentWeather();
    
    // 获取3天天气预报
    bool fetch3DayForecast();
    
    // 获取城市信息
    bool fetchCityInfo();
    
    // 获取器方法
    const String& getCity() const;
    const String& getWeatherText() const;
    const String& getTemperature() const;
    const String& getLastUpdateTime() const;
    const String& getWeatherCode() const;
    const CityInfo& getCityInfo() const;
    const DailyForecast& getForecast(int dayIndex) const;
    
private:
    WiFiManager& wifiManager;
    
    // 当前天气数据
    String city;
    String weatherText;
    String temperature;
    String lastUpdateTime;
    String weatherCode;
    
    // 城市信息
    CityInfo cityInfo;
    
    // 3天天气预报
    DailyForecast forecasts[3];
    
    // 辅助方法
    String weatherToShort(String weatherEn);
    String getWeatherCodeFromText(String weatherEn);
    String base64url_encode(const uint8_t* data, size_t len);
    bool ed25519_sign(const uint8_t* private_key, 
                      const uint8_t* message, 
                      size_t message_len, 
                      uint8_t* signature);
    bool gzipDecompress(uint8_t* compressed, size_t compressedLen, char* decompressed, size_t* decompressedLen);
    String generateJWT();
};

#endif