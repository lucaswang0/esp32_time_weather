#include "WeatherManager.h"
#include <esp_log.h>
#include "DisplayManager.h"

#include <mbedtls/base64.h>
#include "sodium.h"
#include <string.h>
#include "ArduinoUZlib.h"
#include <freertos/FreeRTOS.h>   // 【优化#2】vTaskDelay/pdMS_TO_TICKS

static const char* TAG = "Weather";

static bool sodiumInitialized = false;

static uint8_t compressedData[6144];
static char decompressed[6144];

// 静态工具函数：HTTPS 请求完成后恢复背景缓存（已废弃，保留兼容）
static void restoreBgCacheIfNeeded() {
    // 背景图改为 PROGMEM 数组后无需恢复
}
static JsonDocument doc;

// 新API月相文本映射为图标代码 (800-807)
static String moonPhaseToIcon(const String& phase) {
    if (phase == "new")                return "800";
    if (phase == "waxing-crescent")    return "801";
    if (phase == "first-quarter")      return "802";
    if (phase == "waxing-gibbous")     return "803";
    if (phase == "full")               return "804";
    if (phase == "waning-gibbous")     return "805";
    if (phase == "last-quarter")       return "806";
    if (phase == "waning-crescent")    return "807";
    return phase; // 未知值原样返回
}

WeatherManager::WeatherManager(WiFiManager& wifiManager) : wifiManager(wifiManager), latitude(DEFAULT_LAT), longitude(DEFAULT_LON) {
    if (!sodiumInitialized) {
        if (sodium_init() == 0) {
            sodiumInitialized = true;
            ESP_LOGI(TAG, "libsodium初始化成功");
        } else {
            ESP_LOGE(TAG, "libsodium初始化失败");
        }
    }
}

String WeatherManager::base64url_encode(const uint8_t* data, size_t len) {
    static unsigned char base64_buf[256];
    size_t output_len;
    size_t needed = len * 2 + 10;
    if (needed > sizeof(base64_buf)) {
        ESP_LOGE(TAG, "base64url_encode: buffer too small (%u > %u), len=%u",
                      needed, sizeof(base64_buf), (unsigned)len);
        return "";
    }
    
    int ret = mbedtls_base64_encode(base64_buf, sizeof(base64_buf), &output_len, data, len);
    if (ret != 0) {
        return "";
    }
    
    String result = (char*)base64_buf;
    result.replace('+', '-');
    result.replace('/', '_');
    while (result.endsWith("=")) {
        result.remove(result.length() - 1);
    }
    
    return result;
}

bool WeatherManager::gzipDecompress(uint8_t* compressed, size_t compressedLen, char* decompressed, size_t* decompressedLen) {
    if (compressedLen < 2) return false;
    
    // 检查是否是 gzip 格式 (魔术字节 0x1F 0x8B)
    if (compressed[0] != 0x1F || compressed[1] != 0x8B) {
        // 不是 gzip，直接复制
        if (*decompressedLen >= compressedLen) {
            memcpy(decompressed, compressed, compressedLen);
            *decompressedLen = compressedLen;
            return true;
        }
        return false;
    }
    
    ArduinoUZlib uzlib;
    uint8_t* out_buf = NULL;
    uint32_t out_size = 0;
    
    int32_t result = uzlib.decompress(compressed, compressedLen, out_buf, out_size);
    
    if (result >= 0 && out_buf != NULL && out_size > 0) {
        // 复制到用户提供的缓冲区
        size_t copy_size = (out_size < *decompressedLen) ? out_size : *decompressedLen;
        memcpy(decompressed, out_buf, copy_size);
        *decompressedLen = copy_size;
        decompressed[*decompressedLen] = '\0';
        
        // 释放库分配的内存
        free(out_buf);
        return true;
    }
    
    ESP_LOGE(TAG, "gzip解压失败: %d", result);
    if (out_buf != NULL) {
        free(out_buf);
    }
    return false;
}

bool WeatherManager::ed25519_sign(const uint8_t* private_key, 
                                  const uint8_t* message, 
                                  size_t message_len, 
                                  uint8_t* signature) {
    if (!sodiumInitialized) {
        ESP_LOGI(TAG, "libsodium未初始化");
        return false;
    }
    
    uint8_t public_key[32];
    uint8_t secret_key[64];
    
    crypto_sign_seed_keypair(public_key, secret_key, private_key);
    crypto_sign_detached(signature, NULL, message, message_len, secret_key);
    
    return true;
}

String WeatherManager::generateJWT() {
    unsigned char pkcs8[48];
    size_t pkcs8_len = sizeof(pkcs8);
    
    int ret = mbedtls_base64_decode(pkcs8, sizeof(pkcs8), &pkcs8_len, 
                                   (const unsigned char*)PRIVATE_KEY, strlen(PRIVATE_KEY));
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64解码失败");
        return "";
    }
    
    uint8_t seed[32];
    memcpy(seed, pkcs8 + 16, 32);
    
    String header = "{\"alg\":\"EdDSA\",\"kid\":\"" + String(JWT_KID) + "\"}";
    String payload = "{\"sub\":\"" + String(JWT_SUB) + "\",\"iat\":" + String(time(NULL)) + ",\"exp\":" + String(time(NULL) + 900) + "}";
    
    String header_b64 = base64url_encode((const uint8_t*)header.c_str(), header.length());
    String payload_b64 = base64url_encode((const uint8_t*)payload.c_str(), payload.length());
    
    String signingInput = header_b64 + "." + payload_b64;
    
    uint8_t signature[64];
    if (!ed25519_sign(seed, (const uint8_t*)signingInput.c_str(), signingInput.length(), signature)) {
        return "";
    }
    
    String signature_b64 = base64url_encode(signature, 64);
    
    return signingInput + "." + signature_b64;
}

bool WeatherManager::fetchCurrentWeather() {
    if (!wifiManager.isConnected()) {
        ESP_LOGI(TAG, "WiFi未连接");
        return false;
    }

    // 释放背景 RAM 缓存（已废弃：背景图已改为 PROGMEM 数组，0 RAM 占用）
    ESP_LOGI(TAG, "HTTPS请求前堆内存: %u", ESP.getFreeHeap());
    
    String token = generateJWT();
    if (token == "") {
        ESP_LOGE(TAG, "JWT生成失败");
        return false;
    }
    ESP_LOGI(TAG, "JWT生成成功");
    
    String url = String(QWEATHER_HOST) + "/weather/v1/current/" + latitude + "/" + longitude;

    ESP_LOGI(TAG, "\n========== 获取当前天气 ==========");
    ESP_LOGI(TAG, "[Weather] 请求URL: %s", url.c_str());

    // 【优化#8】重试 3 → 2: 避免 "反复重试" 循环
    // 原来 3 次重试 + 5s + 30s = 1+ 分钟一次失败循环
    // 现在 2 次 (1 次重试) + 5s = 失败后 5s 等待再试 1 次
    // 失败时显式析构 mbedTLS 释放 SSL context (32KB) + placement new 重建
    // (不能直接 WiFi.disconnect(true): 会触发 AP 配网模式循环)
    //
    // 注意: weatherClient 必须在 retry 循环**外**定义, 否则 C++ 默认构造
    // 会与 placement new 冲突, 导致析构时被双重释放
    const int maxRetries = 2;
    WiFiClientSecure weatherClient;  // 循环外定义, 整个函数生命周期
    for (int retry = 0; retry < maxRetries; retry++) {
        // 【优化#3】请求前检查堆**最大连续块** (避免 mbedTLS 内部 malloc 失败)
        // mbedTLS 握手需要 ~16KB 连续内存，HTTP 响应 buffer 还要 ~4KB，合计 > 20KB
        // ESP32-C3 400KB SRAM 启动时堆 130KB+ 但碎片化后 getMaxAllocHeap 远小于 getFreeHeap
        size_t maxAlloc = ESP.getMaxAllocHeap();
        if (maxAlloc < 24 * 1024) {
            ESP_LOGW(TAG, "堆最大连续块不足 24KB (当前: %u B / 剩余: %u B)，放弃本次请求",
                          maxAlloc, ESP.getFreeHeap());
            return false;
        }

        WiFiClientSecure& client = weatherClient;  // 引用, 与外层 weatherClient 同对象
        HTTPClient https;

        client.setInsecure();
        client.setTimeout(5);
        // 缩短握手超时（默认 30s 太长），失败时更快释放资源
        client.setHandshakeTimeout(5000);

        https.begin(client, url);
        https.addHeader("Authorization", "Bearer " + token);
        https.addHeader("Accept-Encoding", "gzip, deflate");
        https.addHeader("User-Agent", "ESP32-Weather");

        ESP_LOGI(TAG, "发送HTTP请求 (第%d/%d次)...", retry + 1, maxRetries);
        int httpCode = https.GET();

        if (httpCode == HTTP_CODE_OK) {
            int len = https.getSize();
            ESP_LOGI(TAG, "HTTP请求成功，数据大小: %d 字节", len);

            if (len > 0 && len < 4096) {
                int bytesRead = https.getStream().readBytes(compressedData, len);
                ESP_LOGI(TAG, "[Weather] 实际读取字节数: %d", bytesRead);
                
                memset(decompressed, 0, sizeof(decompressed));
                size_t decompressedLen = sizeof(decompressed) - 1;
                
                bool isGzip = (bytesRead >= 2 && compressedData[0] == 0x1F && compressedData[1] == 0x8B);
                ESP_LOGI(TAG, "[Weather] 是否gzip压缩: %s", isGzip ? "是" : "否");
                
                if (isGzip) {
                    if (gzipDecompress(compressedData, bytesRead, decompressed, &decompressedLen)) {
                        ESP_LOGI(TAG, "解压后数据大小: %d 字节", decompressedLen);
                        
                        ESP_LOGI(TAG, "完整JSON响应:");
                        ESP_LOGI(TAG, "decompressed");
                        ESP_LOGI(TAG, "");
                        
                        doc.clear();
                        DeserializationError error = deserializeJson(doc, decompressed);
                        
                        if (error) {
                                                        ESP_LOGI(TAG, "error.c_str()");
                            https.end();
                            return false;
                        }
                        
                        // 新API无code字段，检查condition是否存在
                        JsonObject condition = doc["condition"];
                        if (condition.isNull()) {
                            ESP_LOGW(TAG, "API返回异常: 缺少condition字段");
                            ESP_LOGI(TAG, "JSON响应内容:");
                            ESP_LOGI(TAG, "decompressed");
                            https.end();
                            return false;
                        }

                        temperature = doc["temperature"]["value"].as<String>() + "°C";
                        weatherText = condition["text"].as<String>();
                        weatherCode = condition["code"].as<String>();
                        
                        time_t now_t = time(NULL);
                        struct tm timeinfo;
                        localtime_r(&now_t, &timeinfo);
                        char updateTime[20];
                        strftime(updateTime, sizeof(updateTime), "%Y-%m-%d %H:%M:%S", &timeinfo);
                        lastUpdateTime = String(updateTime);
                        
                        ESP_LOGI(TAG, "温度: %s", temperature.c_str());
                        ESP_LOGI(TAG, "天气: %s", weatherText.c_str());
                        ESP_LOGI(TAG, "天气代码: %s", weatherCode.c_str());
                        
                        https.end();
                        return true;
                    } else {
                        ESP_LOGE(TAG, "gzip解压失败");
                        {
                            String hexDump = "";
                            for (int i = 0; i < min(bytesRead, 20); i++) {
                                char buf[4];
                                snprintf(buf, sizeof(buf), "%02X ", compressedData[i]);
                                hexDump += buf;
                            }
                            ESP_LOGE(TAG, "原始数据前20字节: %s", hexDump.c_str());
                        }
                    }
                } else {
                    ESP_LOGI(TAG, "非gzip格式，直接解析");
                    ESP_LOGI(TAG, "完整JSON响应:");
                    ESP_LOGI(TAG, "(const char*)compressedData");
                    ESP_LOGI(TAG, "");
                    
                    doc.clear();
                    DeserializationError error = deserializeJson(doc, (const char*)compressedData);
                    
                    if (error) {
                                                ESP_LOGI(TAG, "error.c_str()");
                    } else {
                        JsonObject condition = doc["condition"];
                        if (!condition.isNull()) {
                            temperature = doc["temperature"]["value"].as<String>() + "°C";
                            weatherText = condition["text"].as<String>();
                            weatherCode = condition["code"].as<String>();
                            ESP_LOGI(TAG, "温度: %s", temperature.c_str());
                            https.end();
                            return true;
                        } else {
                            ESP_LOGW(TAG, "API返回异常: 缺少condition字段");
                        }
                    }
                }
            } else {
                ESP_LOGW(TAG, "数据长度无效: %d 字节", len);
            }
            https.end();
        } else {
            ESP_LOGE(TAG, "请求失败: %d", httpCode);
            ESP_LOGI(TAG, "HTTP状态码说明:");
            ESP_LOGI(TAG, " -1: 连接失败");
            ESP_LOGE(TAG, " -2: DNS解析失败");
            ESP_LOGW(TAG, " -3: 连接超时");
            ESP_LOGE(TAG, " -4: 传输错误");
            ESP_LOGE(TAG, " -5: 无效响应");
            ESP_LOGE(TAG, " 4xx: 请求错误（可能是Token无效）");
            ESP_LOGE(TAG, " 5xx: 服务器错误");
            https.end();
        }
        
        if (retry < maxRetries - 1) {
            https.end();
            // 【优化#2】失败后延时，给 FreeRTOS 回收堆碎片时间
            // 2s 太短，mbedTLS 内部 buffer 还没完全释放；5s 给足够时间回收
            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_LOGW(TAG, "第%d次失败，5秒后重试 (堆剩余: %u B, 最大块: %u B)...",
                          retry + 1, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

            // 【优化#8】主动释放 mbedTLS 内部 buffer: 析构 + placement new 重建
            // mbedTLS 失败后内部 SSL context (32KB) 可能部分残留, 析构强制释放
            // 不能直接调 WiFi.disconnect(true): 会触发 WiFiManager::maintainConnection()
            // 检测到断开后 autoConnect() 失败, 启动 AP 配网模式, 造成循环
            ESP_LOGI(TAG, "主动释放 mbedTLS 内部 buffer...");
            weatherClient.~WiFiClientSecure();
            new (&weatherClient) WiFiClientSecure();
            ESP_LOGI(TAG, "mbedTLS 释放后堆状态: 剩余 %u B, 最大块 %u B",
                          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        }
    }

    ESP_LOGE(TAG, "请求失败");
    restoreBgCacheIfNeeded();
    return false;
}

bool WeatherManager::fetch3DayForecast() {
    if (!wifiManager.isConnected()) {
        ESP_LOGI(TAG, "WiFi未连接");
        return false;
    }

    // 释放背景 RAM 缓存（已废弃：背景图已改为 PROGMEM 数组）
    ESP_LOGI(TAG, "HTTPS请求前堆内存: %u", ESP.getFreeHeap());
    
    String token = generateJWT();
    if (token == "") {
        ESP_LOGE(TAG, "JWT生成失败");
        return false;
    }
    
    String url = String(QWEATHER_HOST) + "/weather/v1/daily/" + latitude + "/" + longitude + "?days=3&localTime=true";
    
    ESP_LOGI(TAG, "\n========== 获取3天天气预报 ==========");
    ESP_LOGI(TAG, "[Weather] 请求URL: %s", url.c_str());
    
    // 【优化#8】重试 3 → 2: 避免 "反复重试" 循环
    // 原来 3 次重试 + 5s + 30s = 1+ 分钟一次失败循环
    // 现在 2 次 (1 次重试) + 5s = 失败后 5s 等待再试 1 次
    // 失败时显式析构 mbedTLS 释放 SSL context (32KB) + placement new 重建
    // (不能直接 WiFi.disconnect(true): 会触发 AP 配网模式循环)
    //
    // 注意: weatherClient 必须在 retry 循环**外**定义, 否则 C++ 默认构造
    // 会与 placement new 冲突, 导致析构时被双重释放
    const int maxRetries = 2;
    WiFiClientSecure weatherClient;  // 循环外定义, 整个函数生命周期
    for (int retry = 0; retry < maxRetries; retry++) {
        // 【优化#3】请求前检查堆**最大连续块** (避免 mbedTLS 内部 malloc 失败)
        // mbedTLS 握手需要 ~16KB 连续内存，HTTP 响应 buffer 还要 ~4KB，合计 > 20KB
        // ESP32-C3 400KB SRAM 启动时堆 130KB+ 但碎片化后 getMaxAllocHeap 远小于 getFreeHeap
        size_t maxAlloc = ESP.getMaxAllocHeap();
        if (maxAlloc < 24 * 1024) {
            ESP_LOGW(TAG, "堆最大连续块不足 24KB (当前: %u B / 剩余: %u B)，放弃本次请求",
                          maxAlloc, ESP.getFreeHeap());
            return false;
        }

        WiFiClientSecure& client = weatherClient;  // 引用, 与外层 weatherClient 同对象
        HTTPClient https;

        client.setInsecure();
        client.setTimeout(5);
        client.setHandshakeTimeout(5000);

        https.begin(client, url);
        https.addHeader("Authorization", "Bearer " + token);
        https.addHeader("Accept-Encoding", "gzip, deflate");
        https.addHeader("User-Agent", "ESP32-Weather");

        ESP_LOGI(TAG, "发送HTTP请求 (第%d/%d次)...", retry + 1, maxRetries);
        int httpCode = https.GET();

        if (httpCode == HTTP_CODE_OK) {
            int len = https.getSize();

            if (len > 0 && len < 4096) {
                int bytesRead = https.getStream().readBytes(compressedData, len);

                memset(decompressed, 0, sizeof(decompressed));
                size_t decompressedLen = sizeof(decompressed) - 1;

                bool isGzip = (bytesRead >= 2 && compressedData[0] == 0x1F && compressedData[1] == 0x8B);

                if (isGzip) {
                    if (gzipDecompress(compressedData, bytesRead, decompressed, &decompressedLen)) {
                        ESP_LOGI(TAG, "完整JSON响应:");
                        ESP_LOGI(TAG, "decompressed");
                        ESP_LOGI(TAG, "");

                        doc.clear();
                        DeserializationError error = deserializeJson(doc, decompressed);
                        
                        if (error) {
                                                        ESP_LOGI(TAG, "error.c_str()");
                            https.end();
                            return false;
                        }
                        
                        // 新API无code字段，检查days数组是否存在
                        JsonArray dailyArray = doc["days"];
                        if (dailyArray.isNull()) {
                            ESP_LOGW(TAG, "API返回异常: 缺少days字段");
                            https.end();
                            return false;
                        }

                        for (int i = 0; i < dailyArray.size() && i < 3; i++) {
                            JsonObject day = dailyArray[i];

                            // forecastStartTime: "2024-08-10T22:00Z" → "2024-08-10"
                            String fs = day["forecastStartTime"].as<String>();
                            forecasts[i].date = fs.length() >= 10 ? fs.substring(0, 10) : fs;
                            forecasts[i].textDay = day["daytime"]["condition"]["text"].as<String>();
                            forecasts[i].tempMin = day["temperatureMin"]["value"].as<String>();
                            forecasts[i].tempMax = day["temperatureMax"]["value"].as<String>();
                            // humidity为0-1小数，转为百分比
                            float hum = day["daytime"]["humidity"].as<float>();
                            forecasts[i].humidity = String((int)(hum * 100));
                            forecasts[i].windDir = day["daytime"]["wind"]["direction"]["compass"].as<String>();
                            forecasts[i].windScale = String(day["daytime"]["wind"]["scale"].as<int>());
                            // sunrise/sunset: "2024-08-11T04:22Z" → "04:22"
                            String sr = day["astro"]["sunrise"].as<String>();
                            forecasts[i].sunrise = sr.length() >= 16 ? sr.substring(11, 16) : sr;
                            String ss = day["astro"]["sunset"].as<String>();
                            forecasts[i].sunset = ss.length() >= 16 ? ss.substring(11, 16) : ss;
                            forecasts[i].moonPhaseIcon = moonPhaseToIcon(day["astro"]["moonPhase"].as<String>());

                            ESP_LOGI(TAG, "第%d天: %s %s %s~%s°C 日出:%s 日落:%s 月相:%s",
                                         i + 1,
                                         forecasts[i].date.c_str(),
                                         forecasts[i].textDay.c_str(),
                                         forecasts[i].tempMin.c_str(),
                                         forecasts[i].tempMax.c_str(),
                                         forecasts[i].sunrise.c_str(),
                                         forecasts[i].sunset.c_str(),
                                         forecasts[i].moonPhaseIcon.c_str());
                        }
                        
                        https.end();
                        return true;
                    } else {
                        ESP_LOGE(TAG, "gzip解压失败");
                    }
                } else {
                    ESP_LOGI(TAG, "完整JSON响应:");
                    ESP_LOGI(TAG, "(const char*)compressedData");
                    ESP_LOGI(TAG, "");
                    
                    doc.clear();
                    DeserializationError error = deserializeJson(doc, (const char*)compressedData);
                    
                    if (error) {
                                                ESP_LOGI(TAG, "error.c_str()");
                    } else {
                        JsonArray dailyArray = doc["days"];
                        if (!dailyArray.isNull()) {
                            for (int i = 0; i < dailyArray.size() && i < 3; i++) {
                                JsonObject day = dailyArray[i];
                                String fs = day["forecastStartTime"].as<String>();
                                forecasts[i].date = fs.length() >= 10 ? fs.substring(0, 10) : fs;
                                forecasts[i].textDay = day["daytime"]["condition"]["text"].as<String>();
                                forecasts[i].tempMin = day["temperatureMin"]["value"].as<String>();
                                forecasts[i].tempMax = day["temperatureMax"]["value"].as<String>();
                                float hum = day["daytime"]["humidity"].as<float>();
                                forecasts[i].humidity = String((int)(hum * 100));
                                forecasts[i].windDir = day["daytime"]["wind"]["direction"]["compass"].as<String>();
                                forecasts[i].windScale = String(day["daytime"]["wind"]["scale"].as<int>());
                                String sr = day["astro"]["sunrise"].as<String>();
                                forecasts[i].sunrise = sr.length() >= 16 ? sr.substring(11, 16) : sr;
                                String ss = day["astro"]["sunset"].as<String>();
                                forecasts[i].sunset = ss.length() >= 16 ? ss.substring(11, 16) : ss;
                                forecasts[i].moonPhaseIcon = moonPhaseToIcon(day["astro"]["moonPhase"].as<String>());
                            }
                            https.end();
                            return true;
                        }
                    }
                }
            }
            https.end();
        } else {
            ESP_LOGE(TAG, "请求失败: %d", httpCode);
            https.end();
        }
        
        if (retry < maxRetries - 1) {
            https.end();
            // 【优化#2】失败后延时，给 FreeRTOS 回收堆碎片时间
            // 2s 太短，mbedTLS 内部 buffer 还没完全释放；5s 给足够时间回收
            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_LOGW(TAG, "第%d次失败，5秒后重试 (堆剩余: %u B, 最大块: %u B)...",
                          retry + 1, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

            // 【优化#8】主动释放 mbedTLS 内部 buffer: 析构 + placement new 重建
            // mbedTLS 失败后内部 SSL context (32KB) 可能部分残留, 析构强制释放
            // 不能直接调 WiFi.disconnect(true): 会触发 WiFiManager::maintainConnection()
            // 检测到断开后 autoConnect() 失败, 启动 AP 配网模式, 造成循环
            ESP_LOGI(TAG, "主动释放 mbedTLS 内部 buffer...");
            weatherClient.~WiFiClientSecure();
            new (&weatherClient) WiFiClientSecure();
            ESP_LOGI(TAG, "mbedTLS 释放后堆状态: 剩余 %u B, 最大块 %u B",
                          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        }
    }

    ESP_LOGE(TAG, "请求失败");
    return false;
}

bool WeatherManager::fetchCityInfo() {
    if (!wifiManager.isConnected()) {
        ESP_LOGI(TAG, "WiFi未连接");
        return false;
    }

    // 释放背景 RAM 缓存（已废弃：背景图已改为 PROGMEM 数组）
    ESP_LOGI(TAG, "HTTPS请求前堆内存: %u", ESP.getFreeHeap());
    
    String token = generateJWT();
    if (token == "") {
        ESP_LOGE(TAG, "JWT生成失败");
        return false;
    }
    
    String url = String(QWEATHER_HOST) + "/geo/v2/city/lookup?location=" + longitude + "%2C" + latitude;
    
    ESP_LOGI(TAG, "\n========== 获取城市信息 ==========");
    ESP_LOGI(TAG, "[Weather] 请求URL: %s", url.c_str());
    
    // 【优化#8】重试 3 → 2: 避免 "反复重试" 循环
    // 原来 3 次重试 + 5s + 30s = 1+ 分钟一次失败循环
    // 现在 2 次 (1 次重试) + 5s = 失败后 5s 等待再试 1 次
    // 失败时显式析构 mbedTLS 释放 SSL context (32KB) + placement new 重建
    // (不能直接 WiFi.disconnect(true): 会触发 AP 配网模式循环)
    //
    // 注意: weatherClient 必须在 retry 循环**外**定义, 否则 C++ 默认构造
    // 会与 placement new 冲突, 导致析构时被双重释放
    const int maxRetries = 2;
    WiFiClientSecure weatherClient;  // 循环外定义, 整个函数生命周期

    for (int retry = 0; retry < maxRetries; retry++) {
        // 【优化#3】请求前检查堆**最大连续块** (避免 mbedTLS 内部 malloc 失败)
        // mbedTLS 握手需要 ~16KB 连续内存，HTTP 响应 buffer 还要 ~4KB，合计 > 20KB
        // ESP32-C3 400KB SRAM 启动时堆 130KB+ 但碎片化后 getMaxAllocHeap 远小于 getFreeHeap
        size_t maxAlloc = ESP.getMaxAllocHeap();
        if (maxAlloc < 24 * 1024) {
            ESP_LOGW(TAG, "堆最大连续块不足 24KB (当前: %u B / 剩余: %u B)，放弃本次请求",
                          maxAlloc, ESP.getFreeHeap());
            return false;
        }

        WiFiClientSecure& client = weatherClient;  // 引用, 与外层 weatherClient 同对象
        HTTPClient https;

        client.setInsecure();
        client.setTimeout(5);
        client.setHandshakeTimeout(5000);

        https.begin(client, url);
        https.addHeader("Authorization", "Bearer " + token);
        https.addHeader("Accept-Encoding", "gzip, deflate");
        https.addHeader("User-Agent", "ESP32-Weather");

        ESP_LOGI(TAG, "发送HTTP请求 (第%d/%d次)...", retry + 1, maxRetries);
        int httpCode = https.GET();

        if (httpCode == HTTP_CODE_OK) {
            int len = https.getSize();
            ESP_LOGI(TAG, "HTTP请求成功，数据大小: %d 字节", len);

            if (len > 0 && len < 4096) {
                int bytesRead = https.getStream().readBytes(compressedData, len);

                memset(decompressed, 0, sizeof(decompressed));
                size_t decompressedLen = sizeof(decompressed) - 1;

                bool isGzip = (bytesRead >= 2 && compressedData[0] == 0x1F && compressedData[1] == 0x8B);
                ESP_LOGI(TAG, "[Weather] 是否gzip压缩: %s", isGzip ? "是" : "否");

                if (isGzip) {
                    if (gzipDecompress(compressedData, bytesRead, decompressed, &decompressedLen)) {
                        ESP_LOGI(TAG, "解压后数据大小: %d 字节", decompressedLen);

                        doc.clear();
                        DeserializationError error = deserializeJson(doc, decompressed);
                        
                        if (error) {
                                                        ESP_LOGI(TAG, "error.c_str()");
                            https.end();
                            return false;
                        }
                        
                        const char* code = doc["code"];
                        if (code == NULL || strcmp(code, "200") != 0) {
                            ESP_LOGW(TAG, "API返回错误码: %s", code ? code : "NULL");
                            https.end();
                            return false;
                        }
                        
                        JsonArray locationArray = doc["location"];
                        if (locationArray.size() > 0) {
                            JsonObject location = locationArray[0];
                            
                            cityInfo.name = location["name"].as<String>();
                            cityInfo.adm1 = location["adm1"].as<String>();
                            cityInfo.adm2 = location["adm2"].as<String>();
                            cityInfo.country = location["country"].as<String>();
                            cityInfo.lat = location["lat"].as<String>();
                            cityInfo.lon = location["lon"].as<String>();
                            
                            city = cityInfo.name;
                            
                            ESP_LOGI(TAG, "获取城市成功: %s", cityInfo.name.c_str());
                        }
                        
                        https.end();
                        return true;
                    } else {
                        ESP_LOGE(TAG, "gzip解压失败");
                    }
                } else {
                    ESP_LOGI(TAG, "非gzip格式，直接解析");
                    doc.clear();
                    DeserializationError error = deserializeJson(doc, (const char*)compressedData);
                    
                    if (error) {
                                                ESP_LOGI(TAG, "error.c_str()");
                    } else {
                        const char* code = doc["code"];
                        if (code != NULL && strcmp(code, "200") == 0) {
                            JsonArray locationArray = doc["location"];
                            if (locationArray.size() > 0) {
                                JsonObject location = locationArray[0];
                                city = location["name"].as<String>();
                                ESP_LOGI(TAG, "获取城市成功: %s", city.c_str());
                                https.end();
                                return true;
                            }
                        }
                    }
                }
            }
            https.end();
        } else {
            ESP_LOGE(TAG, "请求失败: %d", httpCode);
            https.end();
        }
        
        if (retry < maxRetries - 1) {
            https.end();
            // 【优化#2】失败后延时，给 FreeRTOS 回收堆碎片时间
            // 2s 太短，mbedTLS 内部 buffer 还没完全释放；5s 给足够时间回收
            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_LOGW(TAG, "第%d次失败，5秒后重试 (堆剩余: %u B, 最大块: %u B)...",
                          retry + 1, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

            // 【优化#8】主动释放 mbedTLS 内部 buffer: 析构 + placement new 重建
            // mbedTLS 失败后内部 SSL context (32KB) 可能部分残留, 析构强制释放
            // 不能直接调 WiFi.disconnect(true): 会触发 WiFiManager::maintainConnection()
            // 检测到断开后 autoConnect() 失败, 启动 AP 配网模式, 造成循环
            ESP_LOGI(TAG, "主动释放 mbedTLS 内部 buffer...");
            weatherClient.~WiFiClientSecure();
            new (&weatherClient) WiFiClientSecure();
            ESP_LOGI(TAG, "mbedTLS 释放后堆状态: 剩余 %u B, 最大块 %u B",
                          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        }
    }

    ESP_LOGE(TAG, "请求失败");
    restoreBgCacheIfNeeded();
    return false;
}

bool WeatherManager::fetchLocationByIP() {
    if (!wifiManager.isConnected()) {
        ESP_LOGI(TAG, "WiFi未连接");
        return false;
    }

    ESP_LOGI(TAG, "\n========== 通过IP获取定位 ==========");

    // IP 定位服务列表，按优先级排序，前一个失败自动切换下一个
    // format: 0=ip-api.com, 1=ipapi.co/ipwho.is, 2=freeipapi.com, 3=ipinfo.io
    struct IpService {
        const char* url;
        const char* name;
        int format;
        bool useHttps;
    };
    IpService services[] = {
        { "http://ip-api.com/json/?lang=zh-CN", "ip-api.com", 0, false },
        { "https://ipapi.co/json", "ipapi.co", 1, true },
        { "https://ipwho.is/", "ipwho.is", 1, true },
        { "https://free.freeipapi.com/api/json", "freeipapi.com", 2, true },
        { "https://ipinfo.io/json", "ipinfo.io", 3, true },
    };

    String cityName, province, lat, lon;
    bool ipOk = false;

    for (auto& svc : services) {
        ESP_LOGI(TAG, "请求 %s: %s", svc.name, svc.url);

        WiFiClient tcpClient;
        WiFiClientSecure tlsClient;
        HTTPClient http;
        http.setTimeout(10000);

        if (svc.useHttps) {
            tlsClient.setInsecure();
            http.begin(tlsClient, svc.url);
        } else {
            http.begin(tcpClient, svc.url);
        }

        int httpCode = http.GET();

        if (httpCode != HTTP_CODE_OK) {
            ESP_LOGE(TAG, "%s 请求失败: %d", svc.name, httpCode);
            http.end();
            continue;
        }

        String payload = http.getString();
        ESP_LOGI(TAG, "%s 响应: %s", svc.name, payload.c_str());

        doc.clear();
        DeserializationError error = deserializeJson(doc, payload);
        http.end();

        if (error) {
            ESP_LOGE(TAG, "%s JSON解析失败: %s", svc.name, error.c_str());
            continue;
        }

        if (svc.format == 0) {
            // ip-api.com: {"status":"success", "city":"上海", "regionName":"上海市", "lat":31.24, "lon":121.44}
            const char* status = doc["status"];
            if (status == NULL || strcmp(status, "success") != 0) {
                ESP_LOGW(TAG, "ip-api.com 状态异常: %s", status ? status : "NULL");
                continue;
            }
            cityName = doc["city"].as<String>();
            province = doc["regionName"].as<String>();
            lat = doc["lat"].as<String>();
            lon = doc["lon"].as<String>();
        } else if (svc.format == 1) {
            // ipapi.co / ipwho.is: {"city":"Shanghai", "latitude":31.22, "longitude":121.45}
            // ipwho.is 额外有 success 字段
            if (doc["success"].is<bool>() && !doc["success"].as<bool>()) {
                ESP_LOGW(TAG, "%s success=false", svc.name);
                continue;
            }
            lat = doc["latitude"].as<String>();
            lon = doc["longitude"].as<String>();
            cityName = doc["city"].as<String>();
            province = doc["region"].as<String>();
        } else if (svc.format == 2) {
            // freeipapi.com: {"cityName":"Shanghai", "regionName":"Shanghai", "latitude":31.24, "longitude":121.44}
            lat = doc["latitude"].as<String>();
            lon = doc["longitude"].as<String>();
            cityName = doc["cityName"].as<String>();
            province = doc["regionName"].as<String>();
        } else {
            // ipinfo.io: {"city":"Shanghai", "region":"Shanghai", "loc":"31.2222,121.4581"}
            String loc = doc["loc"].as<String>();
            int comma = loc.indexOf(',');
            if (comma > 0) {
                lat = loc.substring(0, comma);
                lon = loc.substring(comma + 1);
            }
            cityName = doc["city"].as<String>();
            province = doc["region"].as<String>();
        }

        if (lat.length() > 0 && lon.length() > 0) {
            ESP_LOGI(TAG, "IP定位成功 (%s): %s %s (%.4f, %.4f)",
                          svc.name, province.c_str(), cityName.c_str(),
                          lat.toFloat(), lon.toFloat());
            ipOk = true;
            break;
        } else {
            ESP_LOGW(TAG, "%s 未获取到经纬度, 尝试下一个服务", svc.name);
        }
    }

    if (!ipOk) {
        ESP_LOGE(TAG, "所有IP定位服务均失败，使用默认经纬度");
        latitude = DEFAULT_LAT;
        longitude = DEFAULT_LON;
        return false;
    }

    // IP定位成功，保存经纬度
    latitude = lat;
    longitude = lon;

    // JWT + QWeather 地理查询 (或降级到 IP 坐标)
    String token = generateJWT();
    if (token == "") {
        ESP_LOGE(TAG, "JWT生成失败，使用IP定位数据作为降级方案");
        city = cityName;
        return true;
    }

    // QWeather 地理查询：location 接受 "lon,lat"
    String geoUrl = String(QWEATHER_HOST) + "/geo/v2/city/lookup?location=" + lon + "%2C" + lat;
    ESP_LOGI(TAG, "[Weather] 和风天气地理查询: %s", geoUrl.c_str());

    size_t maxAlloc = ESP.getMaxAllocHeap();
    if (maxAlloc < 24 * 1024) {
        ESP_LOGW(TAG, "堆最大连续块不足 24KB (当前: %u B), 放弃地理查询", maxAlloc);
        city = cityName;
        return true;
    }

    WiFiClientSecure geoClient;
    HTTPClient https;

    geoClient.setInsecure();
    geoClient.setTimeout(5);
    geoClient.setHandshakeTimeout(5000);
    https.begin(geoClient, geoUrl);
    https.addHeader("Authorization", "Bearer " + token);
    https.addHeader("Accept-Encoding", "gzip, deflate");
    https.addHeader("User-Agent", "ESP32-Weather");

    int geoCode = https.GET();

    if (geoCode == HTTP_CODE_OK) {
        int len = https.getSize();

        if (len > 0 && len < 4096) {
            int bytesRead = https.getStream().readBytes(compressedData, len);

            memset(decompressed, 0, sizeof(decompressed));
            size_t decompressedLen = sizeof(decompressed) - 1;

            bool isGzip = (bytesRead >= 2 && compressedData[0] == 0x1F && compressedData[1] == 0x8B);

            if (isGzip) {
                if (gzipDecompress(compressedData, bytesRead, decompressed, &decompressedLen)) {
                    ESP_LOGI(TAG, "地理查询解压后: %u 字节", (unsigned)decompressedLen);

                    doc.clear();
                    DeserializationError geoError = deserializeJson(doc, decompressed);

                    if (geoError) {
                        ESP_LOGE(TAG, "地理查询JSON解析失败: %s", geoError.c_str());
                        https.end();
                        city = cityName;
                        return true;
                    }

                    const char* geoCodeStr = doc["code"];
                    if (geoCodeStr != NULL && strcmp(geoCodeStr, "200") == 0) {
                        JsonArray locationArray = doc["location"];
                        if (locationArray.size() > 0) {
                            JsonObject location = locationArray[0];
                            locationId = location["id"].as<String>();
                            city = location["name"].as<String>();

                            ESP_LOGI(TAG, "地理查询成功: %s (城市: %s)",
                                          locationId.c_str(), city.c_str());

                            https.end();
                            return true;
                        }
                    }
                    ESP_LOGW(TAG, "地理查询返回错误码: %s", geoCodeStr ? geoCodeStr : "NULL");
                    https.end();
                } else {
                    ESP_LOGE(TAG, "地理查询gzip解压失败");
                    https.end();
                }
            } else {
                ESP_LOGI(TAG, "地理查询非gzip格式，直接解析");

                doc.clear();
                DeserializationError geoError = deserializeJson(doc, (const char*)compressedData);

                if (geoError) {
                    ESP_LOGE(TAG, "地理查询JSON解析失败: %s", geoError.c_str());
                } else {
                    const char* geoCodeStr = doc["code"];
                    if (geoCodeStr != NULL && strcmp(geoCodeStr, "200") == 0) {
                        JsonArray locationArray = doc["location"];
                        if (locationArray.size() > 0) {
                            JsonObject location = locationArray[0];
                            locationId = location["id"].as<String>();
                            city = location["name"].as<String>();

                            ESP_LOGI(TAG, "地理查询成功: %s (城市: %s)",
                                          locationId.c_str(), city.c_str());

                            https.end();
                            return true;
                        }
                    }
                    ESP_LOGW(TAG, "地理查询返回错误码: %s", geoCodeStr ? geoCodeStr : "NULL");
                }
                https.end();
            }
        } else {
            ESP_LOGW(TAG, "地理查询响应大小异常: %d", len);
            https.end();
        }
    } else {
        ESP_LOGE(TAG, "地理查询失败: %d", geoCode);
        https.end();
    }

    // QWeather 地理查询失败，降级使用 IP 坐标
    city = cityName;
    ESP_LOGW(TAG, "使用IP定位数据作为降级方案");
    return true;
}

const String& WeatherManager::getLocationId() const {
    return locationId;
}

const String& WeatherManager::getCity() const {
    return city;
}

const String& WeatherManager::getWeatherText() const {
    return weatherText;
}

const String& WeatherManager::getTemperature() const {
    return temperature;
}

const String& WeatherManager::getLastUpdateTime() const {
    return lastUpdateTime;
}

const String& WeatherManager::getWeatherCode() const {
    return weatherCode;
}

const CityInfo& WeatherManager::getCityInfo() const {
    return cityInfo;
}

const DailyForecast& WeatherManager::getForecast(int dayIndex) const {
    static DailyForecast empty;
    if (dayIndex >= 0 && dayIndex < 3) {
        return forecasts[dayIndex];
    }
    return empty;
}