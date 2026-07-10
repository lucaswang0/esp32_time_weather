#include "PressurePage.h"
#include <SPIFFS.h>
#include <time.h>

// 与 HistoryPage 中 WeatherRecord 完全兼容的内部结构体
struct PressureRecord {
    float temperature;
    float humidity;
    float pressure;
    time_t timestamp;
};

#define ALERT_RECORDS_NEEDED 18   // 3小时 = 18条（每10分钟1条）
#define WARNING_THRESHOLD  -4.0f  // 3小时下降超过4hPa触发警告
#define CAUTION_THRESHOLD  -2.0f  // 3小时下降超过2hPa触发注意

PressurePage::PressurePage(DisplayManager& disp, AHT20BMP280Sensor& aht20)
    : display(disp), aht20(aht20),
      lastPressure(-1.0f), lastChange3h(0.0f),
      lastWarningLevel("--"), lastRecordCount(0),
      lastDrawTime(0) {}

void PressurePage::onEnter() {
    Serial.println("[PressurePage] onEnter");
    // 先刷新历史记录数与3h变化量等显示数据（从 SPIFFS 读取当天文件）
    checkAlert();
    display.clearScreen();
    // 先用薄荷绿覆盖全屏，避免 PNG 背景图残留在 drawPage 之前造成视觉干扰
    // 薄荷绿 (#95D5B2 -> RGB565 0x95B6)
    TFT_eSPI& tft = display.getTFT();
    tft.fillRect(0, 0, 320, 170, 0x95B6);
    drawPage();
}

void PressurePage::update() {
    unsigned long now = millis();

    // 每 10 分钟重算 3h 变化量与预警等级（依赖 SPIFFS 中的历史记录）
    static unsigned long lastReload = 0;
    if (now - lastReload >= 600000) {
        lastReload = now;
        checkAlert();
    }

    // 每 30 秒重绘一次页面（标题时间、当前气压读数随传感器刷新）
    // sensor 每 5 秒更新一次气压，30s 红绘兼顾实时性与闪动频率
    if (now - lastDrawTime >= 30000) {
        lastDrawTime = now;
        drawPage();
    }
}

bool PressurePage::checkAlert() {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char filename[32];
    strftime(filename, sizeof(filename), "/history_%Y-%m-%d.dat", tm_info);

    fs::File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        Serial.printf("[PressurePage] checkAlert: 无法打开文件 %s\n", filename);
        lastChange3h = 0.0f;
        lastWarningLevel = "--";
        lastRecordCount = 0;
        return false;
    }

    int fileCount = 0;
    if (file.available() >= (int)sizeof(fileCount)) {
        file.read((uint8_t*)&fileCount, sizeof(fileCount));
    }

    // 限制读取数量
    if (fileCount > 144) fileCount = 144;

    // 基于文件大小做容错：fileCount 异常（<=0 或与文件大小不匹配）时按实际大小推算
    if (fileCount <= 0) {
        size_t fileSize = file.size();
        if (fileSize >= sizeof(fileCount)) {
            fileCount = (int)((fileSize - sizeof(fileCount)) / sizeof(PressureRecord));
            if (fileCount > 144) fileCount = 144;
            file.seek(sizeof(fileCount), fs::SeekSet);
            Serial.printf("[PressurePage] checkAlert: fileCount 异常，按文件大小推算=%d\n", fileCount);
        } else {
            file.close();
            lastChange3h = 0.0f;
            lastWarningLevel = "--";
            lastRecordCount = 0;
            return false;
        }
    }

    // 读取所有记录到临时数组
    PressureRecord* records = new PressureRecord[fileCount];
    if (!records) {
        file.close();
        Serial.println("[PressurePage] checkAlert: 内存分配失败");
        return false;
    }

    file.read((uint8_t*)records, sizeof(PressureRecord) * fileCount);
    file.close();

    // 取最近 ALERT_RECORDS_NEEDED 条（或全部如果不足）
    int startIdx = 0;
    int useCount = fileCount;
    if (fileCount >= ALERT_RECORDS_NEEDED) {
        startIdx = fileCount - ALERT_RECORDS_NEEDED;
        useCount = ALERT_RECORDS_NEEDED;
    }

    // 计算 3 小时变化量 = 最新 - 最早
    float earliest = records[startIdx].pressure;
    float latest   = records[fileCount - 1].pressure;
    lastChange3h   = latest - earliest;
    lastRecordCount = useCount;

    delete[] records;

    // 判断预警等级
    if (lastChange3h < WARNING_THRESHOLD) {
        lastWarningLevel = "警告";
    } else if (lastChange3h < CAUTION_THRESHOLD) {
        lastWarningLevel = "注意";
    } else {
        lastWarningLevel = "正常";
    }

    Serial.printf("[PressurePage] checkAlert: 3h变化=%.2f hPa | 等级=%s | 记录=%d/%d\n",
        lastChange3h, lastWarningLevel.c_str(), useCount, ALERT_RECORDS_NEEDED);

    // 仅"警告"级别返回 true（触发自动切换页面）
    return (lastWarningLevel == "警告");
}

String PressurePage::getTrendArrow(float change) {
    if (change > 0.5f)  return "\x18";  // 上升 ↑
    if (change < -0.5f) return "\x19";  // 下降 ↓
    return "\x1A";                       // 稳定 →
}

void PressurePage::drawPressureGraph(int x, int y, int w, int h) {
    TFT_eSPI& tft = display.getTFT();
    // 卡片背景 + 边框
    tft.fillRoundRect(x, y, w, h, 3, COLOR_CARD);
    tft.drawRect(x, y, w, h, TFT_RED);

    // 打开当日历史文件
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char filename[32];
    strftime(filename, sizeof(filename), "/history_%Y-%m-%d.dat", tm_info);

    fs::File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        tft.setTextSize(1);
        tft.setTextColor(0xFFFF);  // 白字（薄荷绿背景）
        tft.setCursor(x + w/2 - 30, y + h/2 - 4);
        tft.print("无数据");
        return;
    }

    int fileCount = 0;
    if (file.available() >= (int)sizeof(fileCount)) {
        file.read((uint8_t*)&fileCount, sizeof(fileCount));
    }
    if (fileCount <= 0 || fileCount > 144) {
        size_t fileSize = file.size();
        if (fileSize >= sizeof(fileCount)) {
            fileCount = (int)((fileSize - sizeof(fileCount)) / 20);  // sizeof(PressureRecord)=20
            if (fileCount > 144) fileCount = 144;
            file.seek(sizeof(fileCount), fs::SeekSet);
        } else {
            file.close();
            return;
        }
    }

    if (fileCount < 2) {
        file.close();
        tft.setTextSize(1);
        tft.setTextColor(0xFFFF);  // 白字（薄荷绿背景）
        tft.setCursor(x + w/2 - 30, y + h/2 - 4);
        tft.print("数据不足");
        return;
    }

    // 两遍扫描：第一遍求 min/max，第二遍画线
    // 用栈上 4 个 float 极值（不分配大数组，规避 loop 栈爆栈）
    float pmin = 9999.0f, pmax = -9999.0f;
    PressureRecord rec;
    for (int i = 0; i < fileCount; i++) {
        if (file.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
        if (rec.pressure < pmin) pmin = rec.pressure;
        if (rec.pressure > pmax) pmax = rec.pressure;
    }
    if (pmax == pmin) pmax = pmin + 1.0f;  // 防止除零

    // 重置文件指针，开始画线
    file.seek(sizeof(fileCount), fs::SeekSet);
    int inner_x = x + 2;
    int inner_y = y + 2;
    int inner_w = w - 4;
    int inner_h = h - 4;

    int prev_x = 0, prev_y = 0;
    bool have_prev = false;
    for (int i = 0; i < fileCount; i++) {
        if (file.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) break;
        int px = inner_x + (i * (inner_w - 1)) / (fileCount - 1);
        int py = inner_y + inner_h - 1 - (int)((rec.pressure - pmin) * (inner_h - 1) / (pmax - pmin));
        if (have_prev) {
            tft.drawLine(prev_x, prev_y, px, py, TFT_RED);
        }
        prev_x = px; prev_y = py; have_prev = true;
    }
    file.close();

    // 角落显示 min/max
    tft.setTextSize(1);
    tft.setTextColor(0xFFFF);  // 白字（薄荷绿背景）
    char label[12];
    sprintf(label, "%.0f", pmax);
    tft.setCursor(x + 4, y + 2);
    tft.print(label);
    sprintf(label, "%.0f", pmin);
    tft.setCursor(x + 4, y + h - 10);
    tft.print(label);
}

void PressurePage::drawPage() {
    TFT_eSPI& tft = display.getTFT();

    // 薄荷绿背景 (#95D5B2 -> RGB565 0x95B6)
    tft.fillRect(0, 0, 320, 170, 0x95B6);

    // 标题栏：白色文字（薄荷绿背景）
    tft.setTextColor(0xFFFF);
    tft.setTextSize(1);
    tft.setCursor(8, 6);
    tft.println("气压监测");
    // 右下角更新时间
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M", tm_info);
    tft.setCursor(280, 6);
    tft.printf("%s", time_str);
    // 标题下方分隔线
    tft.drawFastHLine(0, 18, 320, TFT_RED);

    // 当前气压读取
    float currentPressure = aht20.getPressure();
    bool valid = aht20.isValid() && aht20.isBMP280Connected();
    // 气压换算海拔（压高公式）：h = 44330 * (1 - (p/p0)^(1/5.255))，p0 = 1013.25 hPa
    float altitude = 44330.0f * (1.0f - powf(currentPressure / 1013.25f, 1.0f / 5.255f));

    // 中央气压大数字 + 坐标刻度风格
    int value_y = 30;       // 数值区域 y 起点
    int axis_x  = 6;        // 左侧 y 轴标签 x
    int value_x = 20;       // 气压数值 x 起点
    char buf[32];

    tft.setTextColor(0xFFFF);  // 白字（薄荷绿背景）
    tft.setCursor(axis_x, value_y);
    tft.print("hPa");

    tft.setTextColor(0xFFFF);  // 白字（薄荷绿背景）气压主数值
    if (valid) {
        sprintf(buf, "%.1f", currentPressure);
    } else {
        strcpy(buf, "--");
    }
    // 数值行：先填薄荷绿底防止上次残影（与背景一致，自动从屏读）
    tft.fillRect(value_x, value_y, 130, 30, 0x95B6);
    display.drawTextWithBgFont(buf, value_x, value_y, 0xFFFF, font_medium_32);

    // 单位标识：放在数值右侧，紧凑
    tft.setTextColor(0xFFFF);  // 白字
    tft.setCursor(value_x + 125, value_y + 12);
    tft.print("hPa");

    // 海拔（右侧坐标列）
    tft.setTextColor(0xFFFF);  // 白字
    tft.setCursor(220, value_y);
    tft.print("海拔");
    tft.setTextColor(0xFFFF);  // 白字
    tft.setCursor(220, value_y + 14);
    if (valid) {
        tft.printf("%.0f m", altitude);
    } else {
        tft.print("-- m");
    }

    // 区域分隔线
    tft.drawFastHLine(0, 65, 320, TFT_RED);

    // 3小时变化量（标签 size 1，数值 size 2）
    int row1_y = 72;
    tft.setTextSize(1);
    tft.setTextColor(0xFFFF);  // 白字
    tft.setCursor(8, row1_y);
    tft.print("3h");
    tft.setCursor(36, row1_y);
    tft.print("变化量");
    // 数值列（size 2）
    int val_col_x = 110;
    tft.fillRect(val_col_x, row1_y - 2, 200, 18, 0x95B6);
    tft.setTextSize(2);
    if (lastRecordCount >= ALERT_RECORDS_NEEDED) {
        sprintf(buf, "%.1f", lastChange3h);
        if (lastChange3h < CAUTION_THRESHOLD) {
            tft.setTextColor(COLOR_GOLD_WARM);
        } else if (lastChange3h > 0.5f) {
            tft.setTextColor(COLOR_GREEN);
        } else {
            tft.setTextColor(0xFFFF);  // 白字（薄荷绿背景）
        }
        tft.setCursor(val_col_x, row1_y);
        tft.print(buf);
        tft.setTextColor(0xFFFF);  // 白字
        tft.print(" hPa ");
        tft.setTextColor(0xFFFF);  // 白字，趋势箭头
        tft.print(getTrendArrow(lastChange3h));
    } else {
        tft.setTextColor(0xFFFF);  // 白字
        tft.setCursor(val_col_x, row1_y);
        tft.print("积累中...");
    }

    // 区域分隔线
    tft.drawFastHLine(0, 92, 320, TFT_RED);

    // 气压趋势曲线（24h / 144 点）
    drawPressureGraph(4, 95, 312, 48);

    // 区域分隔线
    tft.drawFastHLine(0, 146, 320, TFT_RED);

    // 底部信息行：记录数 + 状态等级（合并成一行，size 1）
    int row3_y = 152;
    tft.setTextSize(1);
    tft.setTextColor(0xFFFF);  // 白字
    tft.setCursor(8, row3_y);
    tft.print("记录");
    tft.setCursor(36, row3_y);
    tft.printf("%d/%d", lastRecordCount, ALERT_RECORDS_NEEDED);
    // 状态等级标签 + 文字
    tft.setCursor(140, row3_y);
    if (lastWarningLevel == "警告") {
        tft.setTextColor(COLOR_GOLD_WARM);
        tft.print("[! 暴风雨 !]");
    } else if (lastWarningLevel == "注意") {
        tft.setTextColor(0xFD20);
        tft.print("[气压下降]");
    } else if (lastWarningLevel == "正常") {
        tft.setTextColor(COLOR_GREEN);
        tft.print("[天气稳定]");
    } else {
        tft.setTextColor(0xFFFF);
        tft.print("[--]");
    }

    // 底部边框
    tft.drawFastHLine(0, 165, 320, TFT_RED);
}
