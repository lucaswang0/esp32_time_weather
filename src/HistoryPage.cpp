#include "HistoryPage.h"
#include "DisplayManager.h"
#include "AHT20BMP280Sensor.h"
#include "config.h"
#include <time.h>
#include <SPIFFS.h>
#include <sys/time.h>

// 中灰背景 (#444444 -> RGB565 0x2108)；黑字改为白色，表现黑底风格
#define COLOR_BG_HISTORY  0x2108
#define COLOR_AXIS        0xFFFF  // 白边框
#define COLOR_GRID        0xC618  // 浅灰网格
#define COLOR_TEMP        0xFBE0  // 亮橙，温度线
#define COLOR_HUMI        0xCFE7  // 亮青，湿度线
#define COLOR_PRESS       0x07FF  // 亮青蓝，气压线
#define COLOR_TEXT        0xFFFF  // 白字（前向替代 TFT_BLACK）

HistoryPage::HistoryPage(DisplayManager& disp, AHT20BMP280Sensor& aht20)
    : display(disp), aht20(aht20), historyCount(0) {
    // 构造期不调用 loadFromSPIFFS()，SPIFFS 尚未挂载且会占用大栈临时数组
    // 数据加载延后到 onEnter() 中执行
}

void HistoryPage::onEnter() {
    Serial.println("[HistoryPage] onEnter");
    loadFromSPIFFS();
    Serial.println("[HistoryPage] draw step 1");
    TFT_eSPI& tft = display.getTFT();
    tft.endWrite();
    tft.fillRect(0, 0, 320, 170, COLOR_BG_HISTORY);
    Serial.println("[HistoryPage] draw step 2");
    drawStatusBar();
    Serial.println("[HistoryPage] draw step 3");
    drawWeatherGraph();
    Serial.println("[HistoryPage] draw step 4");
    drawBottomBar();
    Serial.println("[HistoryPage] draw done");
}

void HistoryPage::update() {
    // 数据保存已移至 main.cpp 的全局定时任务（每10分钟）
    // 此处负责定时刷新图表：
    //   - 每 1 分钟：重绘一次（状态栏时间、底栏记录数）
    //   - 每 10 分钟：从 SPIFFS 重载内存中的 history[]，让图表跟随新增数据点
    unsigned long now = millis();

    static unsigned long lastReload = 0;
    static unsigned long lastRedraw = 0;

    if (now - lastReload >= 600000) {
        lastReload = now;
        loadFromSPIFFS();
    }

    if (now - lastRedraw >= 60000) {
        lastRedraw = now;
        drawStatusBar();
        drawWeatherGraph();
        drawBottomBar();
    }
}

void HistoryPage::addRecord(float temp, float humidity, float pressure) {
    // 每次新数据直接追加到当日文件，不在内存中维护
    // 内存中的 history[] 数组仅在加载时填充，用于绘图显示
    saveRecordToDailyFile(temp, humidity, pressure);
    checkAndCleanOldFiles();
}

void HistoryPage::saveRecordToDailyFile(float temp, float humidity, float pressure) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char filename[32];
    strftime(filename, sizeof(filename), "/history_%Y-%m-%d.dat", tm_info);

    // Append 模式：不分配 2880 字节栈上数组；只读 4 字节 count，再 seek 到尾部追加。
    // 栈占用：filename(32) + rec(20) = 52 字节，比原 2880+32=2912 减少 56 倍。
    int existingCount = 0;
    fs::File readFile = SPIFFS.open(filename, FILE_READ);
    if (readFile) {
        if (readFile.available() >= (int)sizeof(existingCount)) {
            readFile.read((uint8_t*)&existingCount, sizeof(existingCount));
            if (existingCount > MAX_HISTORY_POINTS) existingCount = MAX_HISTORY_POINTS;
        }
        readFile.close();
    }

    // 如果超过 144 条，不再保存（避免无限增长）
    if (existingCount >= MAX_HISTORY_POINTS) {
        Serial.printf("[HistoryPage] %s 已达上限 %d 条，跳过本次保存\n", filename, MAX_HISTORY_POINTS);
        return;
    }

    WeatherRecord rec;
    rec.temperature = temp;
    rec.humidity    = humidity;
    rec.pressure    = pressure;
    rec.timestamp   = now;
    existingCount++;

    bool isNewFile = (existingCount == 1);

    // 分两步写：头部 count 与尾部 record 用不同打开模式
    // 原因：FILE_APPEND ("a") 模式下即使 seek(0) 再 write，也会被强制写到末尾，
    // 导致头部 count 永远保持在第一次写入时的值（=1），记录数显示错误。
    if (!isNewFile) {
        // 已有文件：先以 "r+" 打开，把头部 4 字节 count 覆盖为新值
        fs::File headFile = SPIFFS.open(filename, "r+");
        if (headFile) {
            headFile.seek(0, fs::SeekSet);
            headFile.write((const uint8_t*)&existingCount, sizeof(existingCount));
            headFile.close();
        } else {
            Serial.println("[HistoryPage] saveRecordToDailyFile: head update failed (r+)");
            return;
        }
    }

    // 用 APPEND 模式真实追加 record 到文件末尾
    fs::File file = SPIFFS.open(filename, FILE_APPEND);
    if (!file) {
        Serial.println("[HistoryPage] saveRecordToDailyFile: File open failed (APPEND)");
        return;
    }
    if (isNewFile) {
        // 新文件：以 APPEND 创建后从 0 起，先写 4 字节 count(=1)，再写 record
        file.write((const uint8_t*)&existingCount, sizeof(existingCount));
        file.write((const uint8_t*)&rec, sizeof(rec));
    } else {
        // 已有文件：直接追加 record（头部 count 已在上面更新）
        file.write((const uint8_t*)&rec, sizeof(rec));
    }
    file.close();

    Serial.printf("[HistoryPage] 已保存(平均): T=%.2f°C H=%.2f%% P=%.2fhPa -> %s (%d/%d)\n",
        temp, humidity, pressure, filename, existingCount, MAX_HISTORY_POINTS);
}

void HistoryPage::saveToSPIFFS() {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char filename[32];
    strftime(filename, sizeof(filename), "/history_%Y-%m-%d.dat", tm_info);
    
    fs::File file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("[HistoryPage] saveToSPIFFS: File open failed");
        return;
    }
    
    file.write((const uint8_t*)&historyCount, sizeof(historyCount));
    if (historyCount > 0) {
        file.write((const uint8_t*)history, sizeof(WeatherRecord) * historyCount);
    }
    
    file.close();
    Serial.printf("[HistoryPage] saveToSPIFFS: Saved %d records to %s\n", historyCount, filename);
}

void HistoryPage::loadFromSPIFFS() {
    historyCount = 0;
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    // 只加载当天的数据
    char filename[32];
    strftime(filename, sizeof(filename), "/history_%Y-%m-%d.dat", tm_info);
    
    fs::File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        Serial.printf("[HistoryPage] loadFromSPIFFS: 文件不存在 %s\n", filename);
        return;
    }
    
    int fileCount = 0;
    file.read((uint8_t*)&fileCount, sizeof(fileCount));
    if (fileCount > MAX_HISTORY_POINTS) {
        fileCount = MAX_HISTORY_POINTS;
    }
    
    size_t fileSize = file.size();
    size_t expectedSize = sizeof(fileCount) + sizeof(WeatherRecord) * fileCount;
    if (fileCount <= 0 || fileCount > MAX_HISTORY_POINTS || expectedSize > fileSize) {
        Serial.printf("[HistoryPage] loadFromSPIFFS: fileCount=%d 异常 (%s)，跳过\n", fileCount, filename);
        file.close();
        return;
    }
    
    if (fileCount > 0) {
        WeatherRecord rec;
        for (int i = 0; i < fileCount; i++) {
            if (file.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
            if (historyCount < MAX_HISTORY_BUFFER) {
                history[historyCount++] = rec;
            } else {
                break;
            }
        }
    }
    
    file.close();
    Serial.printf("[HistoryPage] loadFromSPIFFS: Loaded %d records from %s\n", fileCount, filename);
    Serial.printf("[HistoryPage] loadFromSPIFFS: Total %d records loaded\n", historyCount);
}

void HistoryPage::drawStatusBar() {
    TFT_eSPI& tft = display.getTFT();
    // 中灰背景
    tft.fillRect(0, 0, 320, 25, COLOR_BG_HISTORY);
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(1);

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    tft.setCursor(5, 10);
    tft.println(time_str);

    if (historyCount > 0) {
        int last = historyCount - 1;
        // 避免 tft.printf：其内部走 vprintf/vsnprintf 在某些 GCC 实现中栈用量 >2K，
        // 与 GCC -fstack-protector 配合易 spurious 检测到 canary 损坏。
        static char numBuf[8];
        tft.setCursor(155, 10);
        tft.setTextColor(COLOR_TEMP);
        tft.setTextSize(1);
        tft.print("T:");
        tft.print(dtostrf(history[last].temperature, 4, 1, numBuf));
        tft.print("C ");
        tft.setTextColor(COLOR_HUMI);
        tft.print("H:");
        tft.print(dtostrf(history[last].humidity, 4, 1, numBuf));
        tft.print("% ");
        tft.setTextColor(COLOR_PRESS);
        tft.print("P:");
        tft.print(dtostrf(history[last].pressure, 5, 1, numBuf));
        tft.print("hPa");
    }
}

void HistoryPage::drawWeatherGraph() {
    TFT_eSPI& tft = display.getTFT();
    tft.endWrite();

    int graph_x = 10;
    int graph_y = 30;
    int graph_w = 300;
    int graph_h = 110;

    // 背景框
    tft.fillRect(graph_x - 2, graph_y - 2, graph_w + 4, graph_h + 4, COLOR_BG_HISTORY);
    tft.drawRect(graph_x - 2, graph_y - 2, graph_w + 4, graph_h + 4, COLOR_AXIS);

    // 网格线
    tft.setTextColor(COLOR_GRID);
    tft.setTextSize(1);
    for (int y = graph_y; y <= graph_y + graph_h; y += 22) {
        tft.drawLine(graph_x, y, graph_x + graph_w, y, COLOR_GRID);
    }
    for (int x = graph_x; x <= graph_x + graph_w; x += 50) {
        tft.drawLine(x, graph_y, x, graph_y + graph_h, COLOR_GRID);
    }

    // 时间轴标签：按实际时间范围显示
    if (historyCount >= 2) {
        time_t startTs = history[0].timestamp;
        time_t endTs = history[historyCount - 1].timestamp;
        time_t timeRange = endTs - startTs;
        if (timeRange <= 0) timeRange = 1;

        // 显示6个标签点
        for (int i = 0; i <= 6; i++) {
            time_t labelTs = startTs + (timeRange * i) / 6;
            struct tm *labelTm = localtime(&labelTs);
            int hour = labelTm->tm_hour;
            
            int x_pos = graph_x + (graph_w * i) / 6;
            char label[4];
            sprintf(label, "%d", hour);
            tft.setCursor(x_pos - 8, graph_y + graph_h + 2);
            tft.println(label);
        }
    } else {
        // 默认显示 0~24
        for (int h = 0; h <= 24; h += 6) {
            int x_pos = graph_x + (h * graph_w) / 24;
            char label[4];
            sprintf(label, "%d", h);
            tft.setCursor(x_pos - 8, graph_y + graph_h + 2);
            tft.println(label);
        }
    }

    if (historyCount < 2) {
        tft.setTextColor(COLOR_TEXT);
        tft.setCursor(graph_x + 80, graph_y + 50);
        tft.println("等待数据...");
        return;
    }
    
    float temp_min = -10, temp_max = 50;
    float humi_min = 0, humi_max = 100;
    float press_min = 950, press_max = 1050;
    int count = historyCount;

    time_t startTime = history[0].timestamp;
    time_t endTime = history[count - 1].timestamp;
    time_t timeRange = endTime - startTime;
    if (timeRange <= 0) timeRange = 1;

    int step = 1;
    // 要改变 step 的值，以控制曲线的平滑度和数据点的显示密度
    // if (count > 20) step = (count + 20 - 1) / 20;

    // 温度曲线（按时间轴绘制）
    for (int i = 0; i < count - 1; i += step) {
        int i_next = i + step;
        if (i_next >= count) i_next = count - 1;
        int x1 = graph_x + (int)((history[i].timestamp - startTime) * graph_w / timeRange);
        int x2 = graph_x + (int)((history[i_next].timestamp - startTime) * graph_w / timeRange);
        int y1 = graph_y + graph_h - (int)((history[i].temperature - temp_min) * graph_h / (temp_max - temp_min));
        int y2 = graph_y + graph_h - (int)((history[i_next].temperature - temp_min) * graph_h / (temp_max - temp_min));
        tft.drawLine(x1, y1, x2, y2, COLOR_TEMP);
    }
    tft.endWrite();

    // 湿度曲线（按时间轴绘制）
    for (int i = 0; i < count - 1; i += step) {
        int i_next = i + step;
        if (i_next >= count) i_next = count - 1;
        int x1 = graph_x + (int)((history[i].timestamp - startTime) * graph_w / timeRange);
        int x2 = graph_x + (int)((history[i_next].timestamp - startTime) * graph_w / timeRange);
        int y1 = graph_y + graph_h - (int)((history[i].humidity - humi_min) * graph_h / (humi_max - humi_min));
        int y2 = graph_y + graph_h - (int)((history[i_next].humidity - humi_min) * graph_h / (humi_max - humi_min));
        tft.drawLine(x1, y1, x2, y2, COLOR_HUMI);
    }
    tft.endWrite();

    // 气压曲线（按时间轴绘制）
    for (int i = 0; i < count - 1; i += step) {
        int i_next = i + step;
        if (i_next >= count) i_next = count - 1;
        int x1 = graph_x + (int)((history[i].timestamp - startTime) * graph_w / timeRange);
        int x2 = graph_x + (int)((history[i_next].timestamp - startTime) * graph_w / timeRange);
        int y1 = graph_y + graph_h - (int)((history[i].pressure - press_min) * graph_h / (press_max - press_min));
        int y2 = graph_y + graph_h - (int)((history[i_next].pressure - press_min) * graph_h / (press_max - press_min));
        tft.drawLine(x1, y1, x2, y2, COLOR_PRESS);
    }
    tft.endWrite();
}

void HistoryPage::drawBottomBar() {
    TFT_eSPI& tft = display.getTFT();
    // 中灰背景
    tft.fillRect(0, 155, 320, 15, COLOR_BG_HISTORY);
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(1);
    tft.setCursor(5, 160);
    tft.print("Record: ");
    tft.print(historyCount);
    tft.print("  Interval: 10m");

    tft.setCursor(200, 160);
    tft.setTextColor(COLOR_TEMP);
    tft.print("T");
    tft.setTextColor(COLOR_HUMI);
    tft.print(" H");
    tft.setTextColor(COLOR_PRESS);
    tft.print(" P");
}

void HistoryPage::checkAndCleanOldFiles() {
    // 检查SPIFFS空间使用情况
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    float usagePercent = (float)usedBytes / totalBytes * 100;
    
    Serial.printf("[HistoryPage] SPIFFS usage: %.1f%% (%zu/%zu bytes)\n", usagePercent, usedBytes, totalBytes);
    
    // 如果空间使用超过80%，清理最老的文件
    if (usagePercent > 80.0) {
        Serial.println("[HistoryPage] Storage space low, cleaning old files...");
        
        fs::File root = SPIFFS.open("/");
        if (!root) {
            Serial.println("[HistoryPage] Failed to open root directory");
            return;
        }
        
        fs::File file = root.openNextFile();
        String oldestFile = "";
        time_t oldestTime = time(NULL);
        
        while (file) {
            String filename = file.name();
            if (filename.startsWith("/history_") && filename.endsWith(".dat")) {
                // 从文件名提取日期
                int yearStart = filename.indexOf('_') + 1;
                int monthStart = yearStart + 5;
                int dayStart = monthStart + 3;
                
                if (yearStart > 0 && monthStart > 0 && dayStart > 0) {
                    int year = filename.substring(yearStart, yearStart + 4).toInt();
                    int month = filename.substring(monthStart, monthStart + 2).toInt();
                    int day = filename.substring(dayStart, dayStart + 2).toInt();
                    
                    struct tm tm_info = {0};
                    tm_info.tm_year = year - 1900;
                    tm_info.tm_mon = month - 1;
                    tm_info.tm_mday = day;
                    time_t fileTime = mktime(&tm_info);
                    
                    if (fileTime < oldestTime) {
                        oldestTime = fileTime;
                        oldestFile = filename;
                    }
                }
            }
            file = root.openNextFile();
        }
        
        root.close();
        
        // 删除最老的文件
        if (oldestFile.length() > 0) {
            if (SPIFFS.remove(oldestFile)) {
                Serial.printf("[HistoryPage] Deleted old file: %s\n", oldestFile.c_str());
            } else {
                Serial.printf("[HistoryPage] Failed to delete: %s\n", oldestFile.c_str());
            }
        }
    }
}
