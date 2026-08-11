#ifndef PNG_BUFFER_SIZE
#define PNG_BUFFER_SIZE 4096
#endif

#ifndef MAX_IMAGE_WIDTH
#define MAX_IMAGE_WIDTH 320
#endif

#include "DisplayManager.h"
#include <esp_log.h>
#include <LittleFS.h>

static const char* TAG = "Display";

static fs::File pngFile;
static TFT_eSPI* pngTft = nullptr;
static PNG* pngObj = nullptr;
static int pngXpos = 0;
static int pngYpos = 0;
static bool pngIsBackground = false;

void* pngOpen(const char *filename, int32_t *size);
void pngClose(void *handle);
int32_t pngRead(PNGFILE *page, uint8_t *buffer, int32_t length);
int32_t pngSeek(PNGFILE *page, int32_t position);
int pngDraw(PNGDRAW *pDraw);

static const uint16_t* bgSource = nullptr;
static int currentBgIndex = 0;

DisplayManager::DisplayManager() : lastBgDay(-1) {}

void DisplayManager::init() {
    ESP_LOGI(TAG, "初始化显示屏...");

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(0x0000);
    delay(1000);

    if (lastBgDay > 0) {
        int today = lastBgDay;
        randomSeed(today);
        currentBgIndex = random(1, 6);
        ESP_LOGI(TAG, "Time synced, selecting background for day %d: bg%d", today, currentBgIndex);
    } else {
        currentBgIndex = 1;
        ESP_LOGI(TAG, "Time not synced yet, using temporary background: bg1");
    }
    bgSource = getBackgroundByIndex(currentBgIndex);

    tft.setSwapBytes(false);

    uint16_t lineBuf[SCREEN_WIDTH];
    tft.startWrite();
    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        const uint16_t* src = &bgSource[row * SCREEN_WIDTH];
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            lineBuf[col] = __builtin_bswap16(src[col]);
        }
        tft.pushImage(0, row, SCREEN_WIDTH, 1, lineBuf);
    }
    tft.endWrite();
    ESP_LOGI(TAG, "Background image loaded successfully (from PROGMEM)");
    
    tft.setTextDatum(TL_DATUM);
    // 左边区域
    drawTextWithTransparentBgFont("City", 0, 10, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("12:23", 0, 40, COLOR_WHITE, font_large_72);
    drawTextWithTransparentBgFont("56", 78, 28, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("1999.11.22 周日", 0, 106, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("日出:12:34", 0, 130, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("日落:23:45", 0, 150, COLOR_WHITE, font_small_20);
    // 右边区域
    drawTextWithTransparentBgFont("-88", 285, 3, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("风雨", 270, 40, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("11° - 22°", 200, 95, COLOR_WHITE, font_medium_32);
    drawTextWithTransparentBgFont("外:18°", 150, 130, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("内:28°", 150, 150, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("体:18°", 225, 130, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("湿:88%", 225, 150, COLOR_WHITE, font_small_20);

    ESP_LOGI(TAG, "显示屏初始化完成");
}

void DisplayManager::clearScreen() {
    ESP_LOGI(TAG, "栈高水位: %d", uxTaskGetStackHighWaterMark(NULL));

    int today = -1;
    time_t now = time(NULL);
    if (now > 0) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        today = timeinfo.tm_yday;
    }
    
    if (today > 0 && today != lastBgDay) {
        lastBgDay = today;
        randomSeed(today);
        currentBgIndex = random(1, 10);
        bgSource = getBackgroundByIndex(currentBgIndex);
        ESP_LOGI(TAG, "Day changed (%d), selecting new background: bg%d", today, currentBgIndex);
    }
    
    if (bgSource == nullptr) {
        currentBgIndex = 1;
        bgSource = getBackgroundByIndex(currentBgIndex);
    }

    tft.setSwapBytes(false);

    static uint16_t lineBuf[SCREEN_WIDTH];
    tft.startWrite();
    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        const uint16_t* src = &bgSource[row * SCREEN_WIDTH];
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            lineBuf[col] = __builtin_bswap16(src[col]);
        }
        tft.pushImage(0, row, SCREEN_WIDTH, 1, lineBuf);
    }
    tft.endWrite();
}

void DisplayManager::fillBlackScreen() {
    tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, TFT_BLACK);
}

// void DisplayManager::showConnecting() {
//     tft.fillScreen(COLOR_GRAY_LIGHT);
//     tft.fillRoundRect(20, 60, 280, 50, 6, COLOR_CARD);
//     tft.loadFont(font_medium_32);
//     tft.setTextColor(COLOR_GRAY_LIGHT);
//     tft.setCursor(50, 80);
//     tft.print("正在连接WiFi...");
//     tft.unloadFont();
// }

void DisplayManager::showConfigMode() {
    tft.fillScreen(COLOR_GRAY_LIGHT);
    tft.fillRoundRect(30, 20, 260, 110, 6, COLOR_CARD);
    tft.loadFont(font_small_20);
    tft.setTextColor(COLOR_PRIMARY);

    tft.setCursor(50, 30);
    tft.print("WiFi Config Mode");

    tft.setCursor(50, 60);
    tft.setTextColor(COLOR_SUN);
    tft.print("Connect: ESP32-Weather");

    tft.setCursor(50, 85);
    tft.setTextColor(COLOR_GRAY_LIGHT);
    tft.print("pwd: 12345678");

    tft.setCursor(50, 110);
    tft.setTextColor(COLOR_GRAY_MID);
    tft.print("Browser: 192.168.4.1");

    tft.unloadFont();
}

void DisplayManager::fadeOut(int durationMs) {
    int steps = 10;
    int delayMs = durationMs / steps;
    for (int i = 0; i < steps; i++) {
        ledcWrite(BACKLIGHT_CHANNEL, (steps - i) * 255 / steps);
        delay(delayMs);
    }
    ledcWrite(BACKLIGHT_CHANNEL, 0);
}

void DisplayManager::fadeIn(int durationMs) {
    int steps = 10;
    int delayMs = durationMs / steps;
    for (int i = 0; i <= steps; i++) {
        ledcWrite(BACKLIGHT_CHANNEL, i * 255 / steps);
        delay(delayMs);
    }
}

void DisplayManager::drawTextWithBg(const char* text, int x, int y, uint16_t color) {
    drawTextWithBgFont(text, x, y, color, font_small_20);
}

void DisplayManager::drawTextWithBgFont(const char* text, int x, int y, uint16_t color, const uint8_t* font) {
    drawTextWithBgMode(text, x, y, color, font, COLOR_AUTO_FILL);
}

void DisplayManager::drawTextWithTransparentBg(const char* text, int x, int y, uint16_t color) {
    drawTextWithBgMode(text, x, y, color, font_small_20, COLOR_TRANSPARENT);
}

void DisplayManager::drawTextWithTransparentBgFont(const char* text, int x, int y, uint16_t color, const uint8_t* font) {
    drawTextWithBgMode(text, x, y, color, font, COLOR_TRANSPARENT);
}

void DisplayManager::drawTextWithBgMode(const char* text, int x, int y, uint16_t color,
                                          const uint8_t* font, uint16_t bgColor) {

    TFT_eSprite spr = TFT_eSprite(&tft);

    tft.loadFont(font);
    int w = tft.textWidth(text);
    int h = tft.fontHeight();
    tft.unloadFont();

    if (w <= 0 || h <= 0) return;

    spr.createSprite(w, h);

    int bgX = x;
    int bgY = y;

    if (bgColor == COLOR_TRANSPARENT) {
        if (bgSource != nullptr) {
            int readW = w;
            int readH = h;
            int readX = bgX;
            int readY = bgY;
            if (readX < 0) { readW += readX; readX = 0; }
            if (readY < 0) { readH += readY; readY = 0; }
            if (readX + readW > SCREEN_WIDTH) readW = SCREEN_WIDTH - readX;
            if (readY + readH > SCREEN_HEIGHT) readH = SCREEN_HEIGHT - readY;
            if (readW > 0 && readH > 0) {
                spr.fillSprite(0x0001);
                for (int row = 0; row < readH; row++) {
                    int dstY = (readY - bgY) + row;
                    int dstX = readX - bgX;
                    if (dstY >= 0 && dstY < h && dstX >= 0 && dstX + readW <= w) {
                        uint16_t* dstPtr = (uint16_t*)spr.getPointer() + dstY * w + dstX;
                        const uint16_t* srcPtr = &bgSource[(readY + row) * SCREEN_WIDTH + readX];
                        for (int col = 0; col < readW; col++) {
                            dstPtr[col] = __builtin_bswap16(srcPtr[col]);
                        }
                    }
                }
            } else {
                spr.fillSprite(0x0001);
            }
        } else {
            spr.fillSprite(0x8410);
        }
    } else if (bgColor == COLOR_AUTO_FILL) {
        int readX = bgX;
        int readY = bgY;
        int readW = w;
        int readH = h;
        if (bgX < 0) { readW += bgX; readX = 0; }
        if (bgY < 0) { readH += bgY; readY = 0; }
        if (readX + readW > SCREEN_WIDTH) readW = SCREEN_WIDTH - readX;
        if (readY + readH > SCREEN_HEIGHT) readH = SCREEN_HEIGHT - readY;
        if (readW > 0 && readH > 0) {
            tft.readRect(readX, readY, readW, readH, (uint16_t*)spr.getPointer());
        } else {
            spr.fillSprite(0x0001);
        }
    } else {
        spr.fillSprite(bgColor);
    }

    spr.loadFont(font);
    spr.setTextColor(color);
    spr.setCursor(0, 0);
    spr.print(text);
    spr.unloadFont();

    spr.pushSprite(bgX, bgY);
    spr.deleteSprite();
}

void* pngOpen(const char *filename, int32_t *size) {
    ESP_LOGI(TAG, "[PNG] Opening: %s", filename);
    pngFile = LittleFS.open(filename, FILE_READ);
    if (!pngFile) {
        ESP_LOGE(TAG, "[PNG] File open failed: %s", filename);
        *size = 0;
        return NULL;
    }
    *size = pngFile.size();
    return &pngFile;
}

void pngClose(void *handle) {
    (void)handle;
    if (pngFile) {
        pngFile.close();
    }
}

int32_t pngRead(PNGFILE *page, uint8_t *buffer, int32_t length) {
    (void)page;
    if (!pngFile) return 0;
    return pngFile.read(buffer, length);
}

int32_t pngSeek(PNGFILE *page, int32_t position) {
    (void)page;
    if (!pngFile) return 0;
    return pngFile.seek(position);
}

int pngDraw(PNGDRAW *pDraw) {
    if (!pngTft || !pngObj) return 0;
    uint16_t lineBuffer[MAX_IMAGE_WIDTH];
    pngObj->getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);

    if (pngIsBackground) {
        int y = pngYpos + pDraw->y;
        int w = pDraw->iWidth;
        if (w > MAX_IMAGE_WIDTH) w = MAX_IMAGE_WIDTH;
        pngTft->pushImage(pngXpos, y, w, 1, lineBuffer);
    } else {
        int y = pngYpos + pDraw->y;
        const int maskBytes = (pDraw->iWidth + 7) >> 3;
        uint8_t mask[MAX_IMAGE_WIDTH / 8];
        pngObj->getAlphaMask(pDraw, mask, 128);
        for (int x = 0; x < pDraw->iWidth; x++) {
            uint8_t bit = mask[x >> 3] & (0x80 >> (x & 7));
            if (bit) {
                pngTft->drawPixel(pngXpos + x, y, lineBuffer[x]);
            }
        }
        (void)maskBytes;
    }
    return 1;
}

void DisplayManager::drawWeatherIcon(int x, int y, const String& codeStr,
                                      const char* pathPrefix, const char* fallback) {
    // 通用 PNG 图标绘制：路径前缀 + code + ".png"，失败时回退到 fallback（nullptr = 不回退）
    // 现有默认参数兼容原 drawWeatherIcon 行为："/icon_" + code + ".png" → 回退 "/icon_100.png"
    // 月相调用：pathPrefix="/moon_icon_", fallback=nullptr → 失败时空着即可
    if (codeStr.isEmpty()) {
        ESP_LOGI(TAG, "图标代码为空");
        return;
    }

    String iconFile = String(pathPrefix) + codeStr + ".png";
    ESP_LOGI(TAG, "图标代码: %s, 文件: %s", codeStr.c_str(), iconFile.c_str());

    pngTft = &tft;
    pngObj = &png;
    pngXpos = x;
    pngYpos = y;
    pngIsBackground = false;

    int16_t rc = png.open(iconFile.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);
    if (rc != PNG_SUCCESS) {
        if (fallback == nullptr) {
            ESP_LOGE(TAG, "[PNG] %s 打开失败: %d, 无回退配置，跳过", iconFile.c_str(), rc);
            return;
        }
        ESP_LOGW(TAG, "[PNG] %s 打开失败: %d, 尝试回退 %s", iconFile.c_str(), rc, fallback);
        iconFile = String(fallback);
        rc = png.open(iconFile.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);
        if (rc != PNG_SUCCESS) {
            ESP_LOGE(TAG, "[PNG] 回退 %s 也失败: %d", fallback, rc);
            return;
        }
    }

    tft.startWrite();
    ESP_LOGI(TAG, "[PNG] Image specs: (%d x %d), %d bpp", png.getWidth(), png.getHeight(), png.getBpp());

    // 保护背景图：先把图标区域的当前背景图推上去（避免擦除背景图案）
    if (bgSource != nullptr) {
        tft.setSwapBytes(false);
        static uint16_t lineBuf[SCREEN_WIDTH];
        for (int row = 0; row < png.getHeight(); row++) {
            int bgY = y + row;
            if (bgY >= 0 && bgY < SCREEN_HEIGHT) {
                int bgStartIndex = bgY * SCREEN_WIDTH + x;
                for (int col = 0; col < png.getWidth() && x + col < SCREEN_WIDTH; col++) {
                    uint16_t pixel = pgm_read_word(&bgSource[bgStartIndex + col]);
                    lineBuf[col] = (pixel >> 8) | ((pixel & 0xFF) << 8);
                }
                tft.pushImage(x, bgY, png.getWidth(), 1, lineBuf);
            }
        }
        tft.setSwapBytes(true);
    }

    rc = png.decode(NULL, 0);
    tft.endWrite();
    png.close();
}

bool DisplayManager::loadPNGWithBuffer(String filename) {
    ESP_LOGI(TAG, "尝试内存缓冲加载: %s", filename.c_str());
    
    uint32_t freeHeap = ESP.getFreeHeap();
    ESP_LOGI(TAG, "加载前可用内存: %d bytes", freeHeap);
    
    fs::File file = LittleFS.open(filename, "r");
    if (!file) {
        ESP_LOGE(TAG, "❌ 无法打开文件");
        return false;
    }
    
    size_t fileSize = file.size();
    ESP_LOGI(TAG, "文件大小: %d bytes", fileSize);
    
    if (fileSize > 50000) {
        ESP_LOGE(TAG, "❌ 文件太大: %d bytes (限制50KB)", fileSize);
        file.close();
        return false;
    }
    
    if (freeHeap < fileSize + 20000) {
        ESP_LOGE(TAG, "❌ 内存不足: 需要 %d, 可用 %d", fileSize + 20000, freeHeap);
        file.close();
        return false;
    }
    
    uint8_t* fileBuffer = (uint8_t*)malloc(fileSize);
    if (!fileBuffer) {
        ESP_LOGE(TAG, "❌ 内存分配失败");
        file.close();
        return false;
    }
    
    size_t bytesRead = file.read(fileBuffer, fileSize);
    file.close();
    
    if (bytesRead != fileSize) {
        ESP_LOGE(TAG, "❌ 读取不完整: %d/%d bytes", bytesRead, fileSize);
        free(fileBuffer);
        return false;
    }
    
    ESP_LOGI(TAG, "✅ 文件读取成功: %d bytes", bytesRead);
    
    pngTft = &tft;
    pngObj = &png;
    pngXpos = 0;
    pngYpos = 0;
    pngIsBackground = true;
    
    int16_t rc = png.openRAM(fileBuffer, fileSize, pngDraw);
    ESP_LOGI(TAG, "png.openRAM() result: %d", rc);
    
    if (rc == PNG_SUCCESS) {
        int width = png.getWidth();
        int height = png.getHeight();
        int bpp = png.getBpp();
        ESP_LOGI(TAG, "PNG: %dx%d, %d bpp", width, height, bpp);
        
        if (width > 320 || height > 170) {
            ESP_LOGW(TAG, "⚠️ 图片尺寸异常: %dx%d", width, height);
            png.close();
            free(fileBuffer);
            return false;
        }
        
        tft.startWrite();
        rc = png.decode(NULL, 0);
        tft.endWrite();
        png.close();
        
        free(fileBuffer);
        
        if (rc == PNG_SUCCESS) {
            ESP_LOGI(TAG, "✅ 内存缓冲解码成功");
            return true;
        } else {
            ESP_LOGE(TAG, "❌ 内存缓冲解码失败: %d", rc);
            return false;
        }
    } else {
        ESP_LOGE(TAG, "❌ PNG打开失败: %d", rc);
        free(fileBuffer);
        return false;
    }
}

static float calcAltitudeFromPressure(float pressure) {
    if (pressure <= 0) return 0;
    return 44330.0f * (1.0f - powf(pressure / 1013.25f, 0.190284f));
}
