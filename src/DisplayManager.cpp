#ifndef PNG_BUFFER_SIZE
#define PNG_BUFFER_SIZE 4096
#endif

#ifndef MAX_IMAGE_WIDTH
#define MAX_IMAGE_WIDTH 320
#endif

#include "DisplayManager.h"
#include <SPIFFS.h>

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
    Serial.println("[Display] 初始化显示屏...");

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(0x0000);
    delay(1000);

    if (lastBgDay > 0) {
        int today = lastBgDay;
        randomSeed(today);
        currentBgIndex = random(1, 10);
        Serial.printf("[Display] Time synced, selecting background for day %d: bg%d\n", today, currentBgIndex);
    } else {
        currentBgIndex = 1;
        Serial.println("[Display] Time not synced yet, using temporary background: bg1");
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
    Serial.println("[Display] Background image loaded successfully (from PROGMEM)");
    
    tft.setTextDatum(TL_DATUM);
    drawTextWithTransparentBgFont("0000", 0, 10, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("00:00", 0, 40, COLOR_WHITE, font_large_72);
    drawTextWithTransparentBgFont("00", 78, 28, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("日出:00:00", 0, 110, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("日落:00:00", 0, 131, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("0000年00月00日 周X", 0, 150, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("-00", 285, 3, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("0000", 270, 40, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("00° - 00°", 200, 95, COLOR_WHITE, font_medium_32);
    drawTextWithTransparentBgFont("外:--°", 150, 126, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("内:--°", 150, 150, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("体:--°", 225, 126, COLOR_WHITE, font_small_20);
    drawTextWithTransparentBgFont("湿:--%", 225, 150, COLOR_WHITE, font_small_20);

    Serial.println("[Display] 显示屏初始化完成");
}

void DisplayManager::clearScreen() {
    Serial.printf("[Display] 栈高水位: %d\n", uxTaskGetStackHighWaterMark(NULL));

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
        Serial.printf("[Display] Day changed (%d), selecting new background: bg%d\n", today, currentBgIndex);
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

void DisplayManager::showConnecting() {
    tft.fillScreen(COLOR_GRAY_LIGHT);
    tft.fillRoundRect(20, 60, 280, 50, 6, COLOR_CARD);
    tft.loadFont(font_medium_32);
    tft.setTextColor(COLOR_GRAY_LIGHT);
    tft.setCursor(50, 80);
    tft.print("正在连接WiFi...");
    tft.unloadFont();
}

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
    Serial.printf("[PNG] Opening: %s\n", filename);
    pngFile = SPIFFS.open(filename, FILE_READ);
    if (!pngFile) {
        Serial.printf("[PNG] File open failed: %s\n", filename);
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
    pngObj->getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xFFFFFFFF);

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

void DisplayManager::drawWeatherIcon(int x, int y, const String& weatherCodeStr) {
    if (weatherCodeStr.isEmpty()) {
        Serial.println("[Display] 天气代码为空");
        return;
    }
    
    int weatherCode = weatherCodeStr.toInt();
    Serial.printf("[Display] 天气代码: %s -> %d\n", weatherCodeStr.c_str(), weatherCode);
    
    String iconFile = "/icon_" + String(weatherCode) + ".png";
    
    pngTft = &tft;
    pngObj = &png;
    pngXpos = x;
    pngYpos = y;
    pngIsBackground = false;
    
    int16_t rc = png.open(iconFile.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);
    if (rc == PNG_SUCCESS) {
        tft.startWrite();
        Serial.printf("[PNG] Image specs: (%d x %d), %d bpp\n", png.getWidth(), png.getHeight(), png.getBpp());
        
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
    } else {
        Serial.printf("[PNG] Open failed: %d, trying default icon\n", rc);
        
        String defaultFile = "/icon_100.png";
        rc = png.open(defaultFile.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);
        if (rc == PNG_SUCCESS) {
            tft.startWrite();
            Serial.printf("[PNG] Default icon specs: (%d x %d), %d bpp\n", png.getWidth(), png.getHeight(), png.getBpp());
            
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
        } else {
            Serial.println("[PNG] Default icon also failed");
        }
    }
}

bool DisplayManager::loadPNGWithBuffer(String filename) {
    Serial.printf("[Display] 尝试内存缓冲加载: %s\n", filename.c_str());
    
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("[Display] 加载前可用内存: %d bytes\n", freeHeap);
    
    fs::File file = SPIFFS.open(filename, "r");
    if (!file) {
        Serial.println("[Display] ❌ 无法打开文件");
        return false;
    }
    
    size_t fileSize = file.size();
    Serial.printf("[Display] 文件大小: %d bytes\n", fileSize);
    
    if (fileSize > 50000) {
        Serial.printf("[Display] ❌ 文件太大: %d bytes (限制50KB)\n", fileSize);
        file.close();
        return false;
    }
    
    if (freeHeap < fileSize + 20000) {
        Serial.printf("[Display] ❌ 内存不足: 需要 %d, 可用 %d\n", fileSize + 20000, freeHeap);
        file.close();
        return false;
    }
    
    uint8_t* fileBuffer = (uint8_t*)malloc(fileSize);
    if (!fileBuffer) {
        Serial.println("[Display] ❌ 内存分配失败");
        file.close();
        return false;
    }
    
    size_t bytesRead = file.read(fileBuffer, fileSize);
    file.close();
    
    if (bytesRead != fileSize) {
        Serial.printf("[Display] ❌ 读取不完整: %d/%d bytes\n", bytesRead, fileSize);
        free(fileBuffer);
        return false;
    }
    
    Serial.printf("[Display] ✅ 文件读取成功: %d bytes\n", bytesRead);
    
    pngTft = &tft;
    pngObj = &png;
    pngXpos = 0;
    pngYpos = 0;
    pngIsBackground = true;
    
    int16_t rc = png.openRAM(fileBuffer, fileSize, pngDraw);
    Serial.printf("[Display] png.openRAM() result: %d\n", rc);
    
    if (rc == PNG_SUCCESS) {
        int width = png.getWidth();
        int height = png.getHeight();
        int bpp = png.getBpp();
        Serial.printf("[Display] PNG: %dx%d, %d bpp\n", width, height, bpp);
        
        if (width > 320 || height > 170) {
            Serial.printf("[Display] ⚠️ 图片尺寸异常: %dx%d\n", width, height);
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
            Serial.println("[Display] ✅ 内存缓冲解码成功");
            return true;
        } else {
            Serial.printf("[Display] ❌ 内存缓冲解码失败: %d\n", rc);
            return false;
        }
    } else {
        Serial.printf("[Display] ❌ PNG打开失败: %d\n", rc);
        free(fileBuffer);
        return false;
    }
}

static float calcAltitudeFromPressure(float pressure) {
    if (pressure <= 0) return 0;
    return 44330.0f * (1.0f - powf(pressure / 1013.25f, 0.190284f));
}
