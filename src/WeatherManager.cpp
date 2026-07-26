#include "WeatherManager.h"
#include "DisplayManager.h"

#include <mbedtls/base64.h>
#include "sodium.h"
#include <string.h>
#include "ArduinoUZlib.h"
#include <freertos/FreeRTOS.h>   // 【优化#2】vTaskDelay/pdMS_TO_TICKS



static bool sodiumInitialized = false;

static uint8_t compressedData[4096];
static char decompressed[4096];

// 静态工具函数：HTTPS 请求完成后恢复背景缓存（已废弃，保留兼容）
static void restoreBgCacheIfNeeded() {
    // 背景图改为 PROGMEM 数组后无需恢复
}
static JsonDocument doc1024;
static JsonDocument doc2048;

WeatherManager::WeatherManager(WiFiManager& wifiManager) : wifiManager(wifiManager) {
    if (!sodiumInitialized) {
        if (sodium_init() == 0) {
            sodiumInitialized = true;
            Serial.println("[Weather] libsodium初始化成功");
        } else {
            Serial.println("[Weather] libsodium初始化失败");
        }
    }
}

String WeatherManager::base64url_encode(const uint8_t* data, size_t len) {
    size_t output_len;
    unsigned char* base64_buf = (unsigned char*)malloc(len * 2 + 10);
    if (base64_buf == NULL) {
        return "";
    }
    
    int ret = mbedtls_base64_encode(base64_buf, len * 2, &output_len, data, len);
    if (ret != 0) {
        free(base64_buf);
        return "";
    }
    
    String result = (char*)base64_buf;
    free(base64_buf);
    
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
    
    Serial.printf("[Weather] gzip解压失败: %d\n", result);
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
        Serial.println("[Weather] libsodium未初始化");
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
        Serial.println("[Weather] Base64解码失败");
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
        Serial.println("[Weather] WiFi未连接");
        return false;
    }

    // 释放背景 RAM 缓存（已废弃：背景图已改为 PROGMEM 数组，0 RAM 占用）
    Serial.printf("[Weather] HTTPS请求前堆内存: %u\n", ESP.getFreeHeap());
    
    String token = generateJWT();
    if (token == "") {
        Serial.println("[Weather] JWT生成失败");
        return false;
    }
    Serial.println("[Weather] JWT生成成功");
    
    String currentLocation = locationId.isEmpty() ? LOCATION : locationId;
    String url = String(QWEATHER_HOST) + "/v7/weather/now?location=" + currentLocation;

    Serial.println("\n========== 获取当前天气 ==========");
    Serial.println("[Weather] 请求URL: " + url);

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
            Serial.printf("[Weather] 堆最大连续块不足 24KB (当前: %u B / 剩余: %u B)，放弃本次请求\n",
                          maxAlloc, ESP.getFreeHeap());
            return false;
        }

        WiFiClientSecure& client = weatherClient;  // 引用, 与外层 weatherClient 同对象
        HTTPClient https;

        client.setInsecure();
        // 【升级#2】setBufferSizes(16KB recv + 4KB send) 预分配 mbedTLS 静态 buffer
        // 解决 mbedTLS 32KB 连续块分配失败 (-32512)
        // ESP32 Arduino Core 3.x 新增 API, Core 2.x 没有 (会编译错误)
        // 预分配 20KB 静态 buffer 避免运行时 malloc 32KB 连续块
        client.setBufferSizes(16384, 4096);
        client.setTimeout(5);
        // 缩短握手超时（默认 30s 太长），失败时更快释放资源
        client.setHandshakeTimeout(5000);

        https.begin(client, url);
        https.addHeader("Authorization", "Bearer " + token);
        https.addHeader("Accept-Encoding", "gzip, deflate");
        https.addHeader("User-Agent", "ESP32-Weather");

        Serial.printf("[Weather] 发送HTTP请求 (第%d/%d次)...\n", retry + 1, maxRetries);
        int httpCode = https.GET();

        if (httpCode == HTTP_CODE_OK) {
            int len = https.getSize();
            Serial.println("[Weather] HTTP请求成功，数据大小: " + String(len) + " 字节");

            if (len > 0 && len < 4096) {
                int bytesRead = https.getStream().readBytes(compressedData, len);
                Serial.println("[Weather] 实际读取字节数: " + String(bytesRead));
                
                memset(decompressed, 0, sizeof(decompressed));
                size_t decompressedLen = sizeof(decompressed) - 1;
                
                bool isGzip = (bytesRead >= 2 && compressedData[0] == 0x1F && compressedData[1] == 0x8B);
                Serial.println("[Weather] 是否gzip压缩: " + String(isGzip ? "是" : "否"));
                
                if (isGzip) {
                    if (gzipDecompress(compressedData, bytesRead, decompressed, &decompressedLen)) {
                        Serial.println("[Weather] 解压后数据大小: " + String(decompressedLen) + " 字节");
                        
                        Serial.println("[Weather] 完整JSON响应:");
                        Serial.println(decompressed);
                        Serial.println();
                        
                        doc1024.clear();
                        DeserializationError error = deserializeJson(doc1024, decompressed);
                        
                        if (error) {
                            Serial.print("[Weather] JSON解析失败: ");
                            Serial.println(error.c_str());
                            https.end();
                            return false;
                        }
                        
                        const char* code = doc1024["code"];
                        if (code == NULL || strcmp(code, "200") != 0) {
                            Serial.printf("[Weather] API返回错误码: %s\n", code ? code : "NULL");
                            Serial.println("[Weather] JSON响应内容:");
                            Serial.println(decompressed);
                            https.end();
                            return false;
                        }
                        
                        JsonObject now = doc1024["now"];
                        temperature = now["temp"].as<String>() + "°C";
                        weatherText = now["text"].as<String>();
                        weatherCode = now["icon"].as<String>();
                        
                        time_t now_t = time(NULL);
                        struct tm timeinfo;
                        localtime_r(&now_t, &timeinfo);
                        char updateTime[20];
                        strftime(updateTime, sizeof(updateTime), "%Y-%m-%d %H:%M:%S", &timeinfo);
                        lastUpdateTime = String(updateTime);
                        
                        Serial.printf("[Weather] 温度: %s\n", temperature.c_str());
                        Serial.printf("[Weather] 天气: %s\n", weatherText.c_str());
                        Serial.printf("[Weather] 天气代码: %s\n", weatherCode.c_str());
                        
                        https.end();
                        return true;
                    } else {
                        Serial.println("[Weather] gzip解压失败");
                        Serial.println("[Weather] 原始数据前20字节:");
                        for (int i = 0; i < min(bytesRead, 20); i++) {
                            Serial.printf("%02X ", compressedData[i]);
                        }
                        Serial.println();
                    }
                } else {
                    Serial.println("[Weather] 非gzip格式，直接解析");
                    Serial.println("[Weather] 完整JSON响应:");
                    Serial.println((const char*)compressedData);
                    Serial.println();
                    
                    doc1024.clear();
                    DeserializationError error = deserializeJson(doc1024, (const char*)compressedData);
                    
                    if (error) {
                        Serial.print("[Weather] JSON解析失败: ");
                        Serial.println(error.c_str());
                    } else {
                        const char* code = doc1024["code"];
                        if (code != NULL && strcmp(code, "200") == 0) {
                            JsonObject now = doc1024["now"];
                            temperature = now["temp"].as<String>() + "°C";
                            weatherText = now["text"].as<String>();
                            weatherCode = now["icon"].as<String>();
                            Serial.printf("[Weather] 温度: %s\n", temperature.c_str());
                            https.end();
                            return true;
                        } else {
                            Serial.printf("[Weather] API返回错误码: %s\n", code ? code : "NULL");
                        }
                    }
                }
            } else {
                Serial.printf("[Weather] 数据长度无效: %d 字节\n", len);
            }
            https.end();
        } else {
            Serial.printf("[Weather] 请求失败: %d\n", httpCode);
            Serial.println("[Weather] HTTP状态码说明:");
            if (httpCode == -1) Serial.println("         -1: 连接失败");
            if (httpCode == -2) Serial.println("         -2: DNS解析失败");
            if (httpCode == -3) Serial.println("         -3: 连接超时");
            if (httpCode == -4) Serial.println("         -4: 传输错误");
            if (httpCode == -5) Serial.println("         -5: 无效响应");
            if (httpCode >= 400 && httpCode < 500) Serial.println("         4xx: 请求错误（可能是Token无效）");
            if (httpCode >= 500) Serial.println("         5xx: 服务器错误");
            https.end();
        }
        
        if (retry < maxRetries - 1) {
            https.end();
            // 【优化#2】失败后延时，给 FreeRTOS 回收堆碎片时间
            // 2s 太短，mbedTLS 内部 buffer 还没完全释放；5s 给足够时间回收
            vTaskDelay(pdMS_TO_TICKS(5000));
            Serial.printf("[Weather] 第%d次失败，5秒后重试 (堆剩余: %u B, 最大块: %u B)...\n",
                          retry + 1, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

            // 【优化#8】主动释放 mbedTLS 内部 buffer: 析构 + placement new 重建
            // mbedTLS 失败后内部 SSL context (32KB) 可能部分残留, 析构强制释放
            // 不能直接调 WiFi.disconnect(true): 会触发 WiFiManager::maintainConnection()
            // 检测到断开后 autoConnect() 失败, 启动 AP 配网模式, 造成循环
            Serial.println("[Weather] 主动释放 mbedTLS 内部 buffer...");
            weatherClient.~WiFiClientSecure();
            new (&weatherClient) WiFiClientSecure();
            Serial.printf("[Weather] mbedTLS 释放后堆状态: 剩余 %u B, 最大块 %u B\n",
                          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        }
    }

    Serial.println("[Weather] 请求失败");
    restoreBgCacheIfNeeded();
    return false;
}

bool WeatherManager::fetch3DayForecast() {
    if (!wifiManager.isConnected()) {
        Serial.println("[Weather] WiFi未连接");
        return false;
    }

    // 释放背景 RAM 缓存（已废弃：背景图已改为 PROGMEM 数组）
    Serial.printf("[Weather] HTTPS请求前堆内存: %u\n", ESP.getFreeHeap());
    
    String token = generateJWT();
    if (token == "") {
        Serial.println("[Weather] JWT生成失败");
        return false;
    }
    
    String currentLocation = locationId.isEmpty() ? LOCATION : locationId;
    String url = String(QWEATHER_HOST) + "/v7/weather/3d?location=" + currentLocation;
    
    Serial.println("\n========== 获取3天天气预报 ==========");
    Serial.println("[Weather] 请求URL: " + url);
    
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
            Serial.printf("[Weather] 堆最大连续块不足 24KB (当前: %u B / 剩余: %u B)，放弃本次请求\n",
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

        Serial.printf("[Weather] 发送HTTP请求 (第%d/%d次)...\n", retry + 1, maxRetries);
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
                        Serial.println("[Weather] 完整JSON响应:");
                        Serial.println(decompressed);
                        Serial.println();

                        doc2048.clear();
                        DeserializationError error = deserializeJson(doc2048, decompressed);
                        
                        if (error) {
                            Serial.print("[Weather] JSON解析失败: ");
                            Serial.println(error.c_str());
                            https.end();
                            return false;
                        }
                        
                        const char* code = doc2048["code"];
                        if (code == NULL || strcmp(code, "200") != 0) {
                            Serial.printf("[Weather] API返回错误码: %s\n", code ? code : "NULL");
                            https.end();
                            return false;
                        }
                        
                        JsonArray dailyArray = doc2048["daily"];
                        
                        for (int i = 0; i < dailyArray.size() && i < 3; i++) {
                            JsonObject day = dailyArray[i];

                            forecasts[i].date = day["fxDate"].as<String>();
                            forecasts[i].textDay = day["textDay"].as<String>();
                            forecasts[i].tempMin = day["tempMin"].as<String>();
                            forecasts[i].tempMax = day["tempMax"].as<String>();
                            forecasts[i].humidity = day["humidity"].as<String>();
                            forecasts[i].windDir = day["windDirDay"].as<String>();
                            forecasts[i].windScale = day["windScaleDay"].as<String>();
                            forecasts[i].sunrise = day["sunrise"].as<String>();
                            forecasts[i].sunset = day["sunset"].as<String>();
                            forecasts[i].moonPhaseIcon = day["moonPhaseIcon"].as<String>();

                            Serial.printf("[Weather] 第%d天: %s %s %s~%s°C 日出:%s 日落:%s 月相:%s\n",
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
                        Serial.println("[Weather] gzip解压失败");
                    }
                } else {
                    Serial.println("[Weather] 完整JSON响应:");
                    Serial.println((const char*)compressedData);
                    Serial.println();
                    
                    doc2048.clear();
                    DeserializationError error = deserializeJson(doc2048, (const char*)compressedData);
                    
                    if (error) {
                        Serial.print("[Weather] JSON解析失败: ");
                        Serial.println(error.c_str());
                    } else {
                        const char* code = doc2048["code"];
                        if (code != NULL && strcmp(code, "200") == 0) {
                            JsonArray dailyArray = doc2048["daily"];
                            for (int i = 0; i < dailyArray.size() && i < 3; i++) {
                                JsonObject day = dailyArray[i];
                                forecasts[i].date = day["fxDate"].as<String>();
                                forecasts[i].textDay = day["textDay"].as<String>();
                                forecasts[i].tempMin = day["tempMin"].as<String>();
                                forecasts[i].tempMax = day["tempMax"].as<String>();
                                forecasts[i].humidity = day["humidity"].as<String>();
                                forecasts[i].windDir = day["windDirDay"].as<String>();
                                forecasts[i].windScale = day["windScaleDay"].as<String>();
                                forecasts[i].sunrise = day["sunrise"].as<String>();
                                forecasts[i].sunset = day["sunset"].as<String>();
                                forecasts[i].moonPhaseIcon = day["moonPhaseIcon"].as<String>();
                            }
                            https.end();
                            return true;
                        }
                    }
                }
            }
            https.end();
        } else {
            Serial.printf("[Weather] 请求失败: %d\n", httpCode);
            https.end();
        }
        
        if (retry < maxRetries - 1) {
            https.end();
            // 【优化#2】失败后延时，给 FreeRTOS 回收堆碎片时间
            // 2s 太短，mbedTLS 内部 buffer 还没完全释放；5s 给足够时间回收
            vTaskDelay(pdMS_TO_TICKS(5000));
            Serial.printf("[Weather] 第%d次失败，5秒后重试 (堆剩余: %u B, 最大块: %u B)...\n",
                          retry + 1, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

            // 【优化#8】主动释放 mbedTLS 内部 buffer: 析构 + placement new 重建
            // mbedTLS 失败后内部 SSL context (32KB) 可能部分残留, 析构强制释放
            // 不能直接调 WiFi.disconnect(true): 会触发 WiFiManager::maintainConnection()
            // 检测到断开后 autoConnect() 失败, 启动 AP 配网模式, 造成循环
            Serial.println("[Weather] 主动释放 mbedTLS 内部 buffer...");
            weatherClient.~WiFiClientSecure();
            new (&weatherClient) WiFiClientSecure();
            Serial.printf("[Weather] mbedTLS 释放后堆状态: 剩余 %u B, 最大块 %u B\n",
                          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        }
    }

    Serial.println("[Weather] 请求失败");
    return false;
}

bool WeatherManager::fetchCityInfo() {
    if (!wifiManager.isConnected()) {
        Serial.println("[Weather] WiFi未连接");
        return false;
    }

    // 释放背景 RAM 缓存（已废弃：背景图已改为 PROGMEM 数组）
    Serial.printf("[Weather] HTTPS请求前堆内存: %u\n", ESP.getFreeHeap());
    
    String token = generateJWT();
    if (token == "") {
        Serial.println("[Weather] JWT生成失败");
        return false;
    }
    
    String currentLocation = locationId.isEmpty() ? LOCATION : locationId;
    String url = String(QWEATHER_HOST) + "/geo/v2/city/lookup?location=" + currentLocation;
    
    Serial.println("\n========== 获取城市信息 ==========");
    Serial.println("[Weather] 请求URL: " + url);
    
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
            Serial.printf("[Weather] 堆最大连续块不足 24KB (当前: %u B / 剩余: %u B)，放弃本次请求\n",
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

        Serial.printf("[Weather] 发送HTTP请求 (第%d/%d次)...\n", retry + 1, maxRetries);
        int httpCode = https.GET();

        if (httpCode == HTTP_CODE_OK) {
            int len = https.getSize();
            Serial.println("[Weather] HTTP请求成功，数据大小: " + String(len) + " 字节");

            if (len > 0 && len < 4096) {
                int bytesRead = https.getStream().readBytes(compressedData, len);

                memset(decompressed, 0, sizeof(decompressed));
                size_t decompressedLen = sizeof(decompressed) - 1;

                bool isGzip = (bytesRead >= 2 && compressedData[0] == 0x1F && compressedData[1] == 0x8B);
                Serial.println("[Weather] 是否gzip压缩: " + String(isGzip ? "是" : "否"));

                if (isGzip) {
                    if (gzipDecompress(compressedData, bytesRead, decompressed, &decompressedLen)) {
                        Serial.println("[Weather] 解压后数据大小: " + String(decompressedLen) + " 字节");

                        doc1024.clear();
                        DeserializationError error = deserializeJson(doc1024, decompressed);
                        
                        if (error) {
                            Serial.print("[Weather] JSON解析失败: ");
                            Serial.println(error.c_str());
                            https.end();
                            return false;
                        }
                        
                        const char* code = doc1024["code"];
                        if (code == NULL || strcmp(code, "200") != 0) {
                            Serial.printf("[Weather] API返回错误码: %s\n", code ? code : "NULL");
                            https.end();
                            return false;
                        }
                        
                        JsonArray locationArray = doc1024["location"];
                        if (locationArray.size() > 0) {
                            JsonObject location = locationArray[0];
                            
                            cityInfo.name = location["name"].as<String>();
                            cityInfo.adm1 = location["adm1"].as<String>();
                            cityInfo.adm2 = location["adm2"].as<String>();
                            cityInfo.country = location["country"].as<String>();
                            cityInfo.lat = location["lat"].as<String>();
                            cityInfo.lon = location["lon"].as<String>();
                            
                            city = cityInfo.name;
                            
                            Serial.printf("[Weather] 获取城市成功: %s\n", cityInfo.name.c_str());
                        }
                        
                        https.end();
                        return true;
                    } else {
                        Serial.println("[Weather] gzip解压失败");
                    }
                } else {
                    Serial.println("[Weather] 非gzip格式，直接解析");
                    doc1024.clear();
                    DeserializationError error = deserializeJson(doc1024, (const char*)compressedData);
                    
                    if (error) {
                        Serial.print("[Weather] JSON解析失败: ");
                        Serial.println(error.c_str());
                    } else {
                        const char* code = doc1024["code"];
                        if (code != NULL && strcmp(code, "200") == 0) {
                            JsonArray locationArray = doc1024["location"];
                            if (locationArray.size() > 0) {
                                JsonObject location = locationArray[0];
                                city = location["name"].as<String>();
                                Serial.printf("[Weather] 获取城市成功: %s\n", city.c_str());
                                https.end();
                                return true;
                            }
                        }
                    }
                }
            }
            https.end();
        } else {
            Serial.printf("[Weather] 请求失败: %d\n", httpCode);
            https.end();
        }
        
        if (retry < maxRetries - 1) {
            https.end();
            // 【优化#2】失败后延时，给 FreeRTOS 回收堆碎片时间
            // 2s 太短，mbedTLS 内部 buffer 还没完全释放；5s 给足够时间回收
            vTaskDelay(pdMS_TO_TICKS(5000));
            Serial.printf("[Weather] 第%d次失败，5秒后重试 (堆剩余: %u B, 最大块: %u B)...\n",
                          retry + 1, ESP.getFreeHeap(), ESP.getMaxAllocHeap());

            // 【优化#8】主动释放 mbedTLS 内部 buffer: 析构 + placement new 重建
            // mbedTLS 失败后内部 SSL context (32KB) 可能部分残留, 析构强制释放
            // 不能直接调 WiFi.disconnect(true): 会触发 WiFiManager::maintainConnection()
            // 检测到断开后 autoConnect() 失败, 启动 AP 配网模式, 造成循环
            Serial.println("[Weather] 主动释放 mbedTLS 内部 buffer...");
            weatherClient.~WiFiClientSecure();
            new (&weatherClient) WiFiClientSecure();
            Serial.printf("[Weather] mbedTLS 释放后堆状态: 剩余 %u B, 最大块 %u B\n",
                          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        }
    }

    Serial.println("[Weather] 请求失败");
    restoreBgCacheIfNeeded();
    return false;
}

bool WeatherManager::fetchLocationByIP() {
    if (!wifiManager.isConnected()) {
        Serial.println("[Weather] WiFi未连接");
        return false;
    }

    Serial.println("\n========== 通过IP获取定位 ==========");

    String url = "http://ip-api.com/json/?lang=zh-CN";
    Serial.println("[Weather] 请求URL: " + url);

    WiFiClient client;
    HTTPClient http;
    
    http.begin(client, url);
    http.setTimeout(10000);
    
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("[Weather] IP定位响应:");
        Serial.println(payload);
        
        doc1024.clear();
        DeserializationError error = deserializeJson(doc1024, payload);
        
        if (error) {
            Serial.print("[Weather] IP定位JSON解析失败: ");
            Serial.println(error.c_str());
            http.end();
            return false;
        }
        
        const char* status = doc1024["status"];
        if (status == NULL || strcmp(status, "success") != 0) {
            Serial.printf("[Weather] IP定位失败: %s\n", status ? status : "NULL");
            http.end();
            return false;
        }
        
        String cityName = doc1024["city"].as<String>();
        String province = doc1024["regionName"].as<String>();
        String lat = doc1024["lat"].as<String>();
        String lon = doc1024["lon"].as<String>();
        
        Serial.printf("[Weather] IP定位成功: %s %s (%.4f, %.4f)\n", 
                      province.c_str(), cityName.c_str(), 
                      lat.toFloat(), lon.toFloat());
        
        if (lat.length() == 0 || lon.length() == 0) {
            Serial.println("[Weather] 未获取到经纬度");
            http.end();
            return false;
        }

        String token = generateJWT();
        if (token == "") {
            Serial.println("[Weather] JWT生成失败");
            http.end();
            return false;
        }

        // QWeather 地理查询：location 接受 "lon,lat"（逗号需 URL 编码为 %2C）
        String geoUrl = String(QWEATHER_HOST) + "/geo/v2/city/lookup?location=" + lon + "%2C" + lat;
        Serial.println("[Weather] 和风天气地理查询: " + geoUrl);

        // 【优化#3】请求前检查堆**最大连续块** (避免 mbedTLS 内部 malloc 失败)
        // mbedTLS 握手需要 ~16KB 连续内存，HTTP 响应 buffer 还要 ~4KB，合计 > 20KB
        // ESP32-C3 400KB SRAM 启动时堆 130KB+ 但碎片化后 getMaxAllocHeap 远小于 getFreeHeap
        size_t maxAlloc = ESP.getMaxAllocHeap();
        if (maxAlloc < 24 * 1024) {
            Serial.printf("[Weather] 堆最大连续块不足 24KB (当前: %u B / 剩余: %u B)，放弃地理查询\n",
                          maxAlloc, ESP.getFreeHeap());
            http.end();
            return false;
        }

        WiFiClientSecure geoClient;
        HTTPClient https;

        geoClient.setInsecure();
        // 【升级#2】setBufferSizes 预分配 mbedTLS 静态 buffer (同上)
        geoClient.setBufferSizes(16384, 4096);
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
                        Serial.println("[Weather] 地理查询解压后数据大小: " + String(decompressedLen) + " 字节");

                        doc1024.clear();
                        DeserializationError geoError = deserializeJson(doc1024, decompressed);

                        if (geoError) {
                            Serial.print("[Weather] 地理查询JSON解析失败: ");
                            Serial.println(geoError.c_str());
                            https.end();
                            http.end();
                            return false;
                        }

                        const char* geoCodeStr = doc1024["code"];
                        if (geoCodeStr != NULL && strcmp(geoCodeStr, "200") == 0) {
                            JsonArray locationArray = doc1024["location"];
                            if (locationArray.size() > 0) {
                                JsonObject location = locationArray[0];
                                locationId = location["id"].as<String>();
                                city = location["name"].as<String>();

                                Serial.printf("[Weather] 获取LOCATION ID成功: %s (城市: %s)\n",
                                              locationId.c_str(), city.c_str());

                                https.end();
                                http.end();
                                return true;
                            }
                        }
                        Serial.printf("[Weather] 地理查询返回错误码: %s\n", geoCodeStr ? geoCodeStr : "NULL");
                        https.end();
                    } else {
                        Serial.println("[Weather] 地理查询gzip解压失败");
                        https.end();
                    }
                } else {
                    Serial.println("[Weather] 地理查询非gzip格式，直接解析");

                    doc1024.clear();
                    DeserializationError geoError = deserializeJson(doc1024, (const char*)compressedData);

                    if (geoError) {
                        Serial.print("[Weather] 地理查询JSON解析失败: ");
                        Serial.println(geoError.c_str());
                    } else {
                        const char* geoCodeStr = doc1024["code"];
                        if (geoCodeStr != NULL && strcmp(geoCodeStr, "200") == 0) {
                            JsonArray locationArray = doc1024["location"];
                            if (locationArray.size() > 0) {
                                JsonObject location = locationArray[0];
                                locationId = location["id"].as<String>();
                                city = location["name"].as<String>();

                                Serial.printf("[Weather] 获取LOCATION ID成功: %s (城市: %s)\n",
                                              locationId.c_str(), city.c_str());

                                https.end();
                                http.end();
                                return true;
                            }
                        }
                        Serial.printf("[Weather] 地理查询返回错误码: %s\n", geoCodeStr ? geoCodeStr : "NULL");
                    }
                    https.end();
                }
            } else {
                Serial.printf("[Weather] 地理查询响应大小异常: %d\n", len);
                https.end();
            }
        } else {
            Serial.printf("[Weather] 地理查询失败: %d\n", geoCode);
            https.end();
        }
        http.end();
    } else {
        Serial.printf("[Weather] IP定位请求失败: %d\n", httpCode);
        http.end();
    }
    
    Serial.println("[Weather] IP定位失败，使用默认LOCATION");
    locationId = LOCATION;
    return false;
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