// XFont 实现（从 tftziku 移植，改 SPIFFS + sprite 画布支持）
#include <Arduino.h>
#include "xfont.h"
#include <TFT_eSPI.h>
#include <esp_log.h>


static const char *TAG = "XFont";

// C++11 委托构造，修复原版 XFont(){XFont(false);} 创建临时对象的 bug
XFont::XFont() : XFont(false) {}

XFont::XFont(bool isTFT)
{
    unsigned long beginTime = millis();
    if (isTFT == true)
    {
        InitTFT();
        ESP_LOGI(TAG, "     TFT初始化耗时:%2f 秒.\r\n", (millis() - beginTime) / 1000.0);
    }

    beginTime = millis();
    initZhiku(fontFilePath);
    ESP_LOGI(TAG, "     装载字符集耗时:%2f 秒.\r\n", (millis() - beginTime) / 1000.0);
}

XFont::~XFont(void)
{
    clear();
}

void XFont::clear(void)
{
    if (fontFile)
        fontFile.close();
    strAllUnicodes = String("");
}

void XFont::InitTFT()
{
    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN);
    isTftInited = true;
}

// 转化字符数组为字符串（基于显式长度，避免越界读取非NUL终止缓冲区）
String XFont::getStringFromChars(uint8_t *bs, int l)
{
    String ret;
    ret.reserve(l);
    for (int i = 0; i < l; i++) {
        ret += (char)bs[i];
    }
    return ret;
}

// 把 utf8 编码字符转 unicode 编码
String XFont::getUnicodeFromUTF8(String s)
{
    char character[s.length()];
    String string_to_hex = "";
    int n = 0;
    for (uint16_t i = 0; i < s.length(); i++)
    {
        if (s[i] >= 1 and s[i] <= 127)
        {
            String ss1 = String(s[i], HEX);
            ss1 = ss1.length() == 1 ? "0" + ss1 : ss1;
            string_to_hex += "00" + ss1;
        }
        else
        {
            character[n + 1] = ((s[i + 1] & 0x3) << 6) + (s[i + 2] & 0x3F);
            character[n] = ((s[i] & 0xF) << 4) + ((s[i + 1] >> 2) & 0xF);
            String ss1 = String(character[n], HEX);
            String ss2 = String(character[n + 1], HEX);
            string_to_hex += ss1.length() == 1 ? "0" + ss1 : ss1;
            string_to_hex += ss2.length() == 1 ? "0" + ss2 : ss2;
            n = n + 2;
            i = i + 2;
        }
    }
    return string_to_hex;
}

// 依照字号和编码方式计算每个字符存储展位
int XFont::getFontPage(int font_size, int bin_type)
{
    int total = font_size * font_size;
    int hexCount = 8;
    if (bin_type == 32)
        hexCount = 10;
    if (bin_type == 64)
        hexCount = 12;
    int hexAmount = int(total / hexCount);
    if (total % hexCount > 0)
    {
        hexAmount += 1;
    }
    return hexAmount * 2;
}

// 从字符的像素 64 进制字符重新转成二进制字符串
String XFont::getPixDataFromHex(String s)
{
    int l = s.length();
    int cnt = 0;
    String ret = "";
    int cc = 5;

    for (int ii = 0; ii < l; ii++)
    {
        int d = strchr(s64, s[ii]) - s64;
        for (int k = cc; k >= 0; k--)
        {
            ret += (String)bitRead(d, k);
            cnt++;
        }
    }
    return ret;
}

void XFont::reInitZhiku(String fontPath)
{
    isInit = false;
    initZhiku(fontPath);
}

void XFont::initZhiku(String fontPath)
{
    if (isInit == true)
        return;
    if (LittleFS.begin() == false)
    {
        ESP_LOGE(TAG, "SPIFFS 文件系统初始化失败,请检查相关配置。");
        return;
    }
    if (LittleFS.exists(fontPath))
    {
        fontFile = LittleFS.open(fontPath, "r");

        static uint8_t buf_total_str[6];
        static uint8_t buf_font_size[2];
        static uint8_t buf_bin_type[2];
        fontFile.read(buf_total_str, 6);
        fontFile.read(buf_font_size, 2);
        fontFile.read(buf_bin_type, 2);
        String s1 = getStringFromChars(buf_total_str, 6);
        String s2 = getStringFromChars(buf_font_size, 2);
        String s3 = getStringFromChars(buf_bin_type, 2);
        total_font_cnt = strtoll(s1.c_str(), NULL, 16);
        font_size = s2.toInt();
        bin_type = s3.toInt();
        font_page = getFontPage(font_size, bin_type);
        font_unicode_cnt = total_font_cnt * 5;

        strAllUnicodes = "";
        uint8_t *buf_total_str_unicode2;
        int laststr = font_unicode_cnt;
        int read_size = 512 * 2;
        buf_total_str_unicode2 = (uint8_t *)malloc(read_size);
        if (buf_total_str_unicode2 == NULL) {
            ESP_LOGE(TAG, "[XFont] 字库索引缓冲分配失败");
            fontFile.close();
            return;
        }
        do
        {
            size_t k = read_size;
            if (laststr < read_size)
                k = laststr;
            fontFile.read(buf_total_str_unicode2, k);
            strAllUnicodes += getStringFromChars(buf_total_str_unicode2, k);
            laststr -= read_size;
        } while (laststr > 0);
        free(buf_total_str_unicode2);

        ESP_LOGI(TAG, "     字库中字符总数:%d \r\n", strAllUnicodes.length() / 5);
        unicode_begin_idx = 6 + 2 + 2 + total_font_cnt * 5;
        isInit = true;
        fontFile.close();
    }
    else
    {
        ESP_LOGI(TAG, "SPIFFS 系统工作正常，但是找不到字库文件。");
    }
}

// 从字库文件获取字符对应的二进制编码字符串
String XFont::getPixBinStrFromString(String strUnicode)
{
    initZhiku(fontFilePath);
    String ret = "";
    if (!fontFile)
    {
        fontFile = LittleFS.open(fontFilePath, "r");
        uint8_t buf_seek_pixdata[font_page];
        const char *chrAllUnicodes = strAllUnicodes.c_str();
        for (uint16_t i = 0; i < strUnicode.length(); i = i + 4)
        {
            String _str = "u" + strUnicode.substring(i, i + 4);

            int p = 0;
            int uIdx = 0;
            char *chrFind = strstr(chrAllUnicodes, _str.c_str());
            p = chrFind - chrAllUnicodes;
            uIdx = p / 5;
            int pixbeginidx = unicode_begin_idx + uIdx * font_page;
            fontFile.seek(pixbeginidx);
            fontFile.read(buf_seek_pixdata, font_page);
            String su = getStringFromChars(buf_seek_pixdata, font_page);
            String ts = getPixDataFromHex(su);
            ret += ts;
        }
        fontFile.close();
    }
    return ret;
}

// 从字库文件获取字符对应的编码字符串
String XFont::getCodeDataFromFile(String strUnicode)
{
    String ret = "";
    if (!fontFile)
    {
        fontFile = LittleFS.open(fontFilePath, "r");
        uint8_t buf_seek_pixdata[font_page];
        const char *chrAllUnicodes = strAllUnicodes.c_str();
        for (uint16_t i = 0; i < strUnicode.length(); i = i + 4)
        {
            String _str = "u" + strUnicode.substring(i, i + 4);
            int p = 0;
            int uIdx = 0;
            char *chrFind = strstr(chrAllUnicodes, _str.c_str());
            p = chrFind - chrAllUnicodes;
            uIdx = p / 5;
            int pixbeginidx = unicode_begin_idx + uIdx * font_page;
            fontFile.seek(pixbeginidx);
            fontFile.read(buf_seek_pixdata, font_page);
            String su = getStringFromChars(buf_seek_pixdata, font_page);
            ret += su;
        }
        fontFile.close();
    }
    return ret;
}

// 判断是否 ansi 字符
bool XFont::chkAnsi(unsigned char c)
{
    if (c >= 0 && c <= 127)
        return true;
    return false;
}

// 带 backColor 版本：每个像素都绘制（前景或背景）
void XFont::DrawSingleStr(int x, int y, String strBinData, int fontColor, int backColor, bool ansiChar)
{
    // _target 优先（DisplayManager 传入的 sprite 或外部 tft），否则用内部 tft
    TFT_eSPI *dst = _target ? _target : &tft;
    for (uint16_t i = 0; i < strBinData.length(); i++)
    {
        int pX1 = int(i % font_size) + x;
        int pY1 = int(i / font_size) + y;
        if (ansiChar)
        {
            uint16_t brk = i % font_size;
            if (brk > (font_size / 2))
                continue;
        }
        if (strBinData[i] == '1')
        {
            dst->fillRect(pX1, pY1, 1, 1, fontColor);
        }
        else
        {
            dst->fillRect(pX1, pY1, 1, 1, backColor);
        }
    }
}

// 透明背景版本：只绘制前景像素
void XFont::DrawSingleStr(int x, int y, String strBinData, int fontColor, bool ansiChar)
{
    TFT_eSPI *dst = _target ? _target : &tft;
    for (uint16_t i = 0; i < strBinData.length(); i++)
    {
        if (strBinData[i] == '1')
        {
            int pX1 = int(i % font_size) + x;
            int pY1 = int(i / font_size) + y;
            if (ansiChar)
            {
                uint16_t brk = i % font_size;
                if (brk > (font_size / 2))
                    continue;
            }
            dst->fillRect(pX1, pY1, 1, 1, fontColor);
        }
    }
}

String XFont::GetPixDatasFromLib(String displayStr)
{
    initZhiku(fontFilePath);
    if (isInit == false)
    {
        ESP_LOGE(TAG, "字库初始化失败");
        return "";
    }
    String ret = "";
    String strUnicode = getUnicodeFromUTF8(displayStr);
    for (uint16_t l = 0; l < strUnicode.length() / 4; l++)
    {
        String childUnicode = strUnicode.substring(4 * l, (4) + 4 * l);
        ret += getPixBinStrFromString(childUnicode);
    }
    return ret;
}

// DrawStr: 边读字库边显示
void XFont::DrawStr(int x, int y, String str, int fontColor, int backColor)
{
    initZhiku(fontFilePath);
    if (isInit == false)
    {
        ESP_LOGE(TAG, "字库初始化失败");
        return;
    }

    String strUnicode = getUnicodeFromUTF8(str);
    singleStrPixsAmount = font_size * font_size;

    int px = x;
    int py = y;

    for (uint16_t l = 0; l < strUnicode.length() / 4; l++)
    {
        String childUnicode = strUnicode.substring(4 * l, (4) + 4 * l);
        String childPixData = getPixBinStrFromString(childUnicode);
        u_int sep = 1; // 字间距
        int f = 0;
        sscanf(childUnicode.c_str(), "%x", &f);

        if (f <= 127) // ANSI 字符
        {
            if (f == 13 || f == 10) // \r\n
            {
                px = 0;
                py += font_size + sep;
                continue;
            }
            else if (f == 9) // \t
            {
                px += font_size + sep;
                continue;
            }

            if ((px + font_size / 2) > screenWidth)
            {
                px = 0;
                py += font_size + sep;
            }
            if (backColor == -1)
                DrawSingleStr(px, py, childPixData, fontColor, true);
            else
                DrawSingleStr(px, py, childPixData, fontColor, backColor, true);

            px += font_size / 2 + sep;
        }
        else // 中文字符
        {
            if ((px + font_size) > screenWidth)
            {
                px = 0;
                py += font_size + sep;
            }
            if (backColor == -1)
                DrawSingleStr(px, py, childPixData, fontColor, false);
            else
                DrawSingleStr(px, py, childPixData, fontColor, backColor, false);
            px += font_size + sep;
        }
    }
}

void XFont::DrawStr(int x, int y, String str, int fontColor)
{
    DrawStr(x, y, str, fontColor, -1);
}

void XFont::DrawChinese(int x, int y, String str, int fontColor)
{
    DrawChinese(x, y, str, fontColor, -1);
}

void XFont::DrawChinese(int x, int y, String str, int fontColor, int backColor)
{
    // isTftInited 默认 true（DisplayManager 已 init TFT），直接绘制
    DrawStr(x, y, str, fontColor, backColor);
}

void XFont::DrawChineseEx(int x, int y, String str, int fontColor, int backColor)
{
    DrawStrEx(x, y, str, fontColor, backColor);
}

void XFont::DrawChineseEx(int x, int y, String str, int fontColor)
{
    DrawChineseEx(x, y, str, fontColor, -1);
}

// DrawStrEx: 一次性读字库再统一显示
void XFont::DrawStrEx(int x, int y, String str, int fontColor, int backColor)
{
    initZhiku(fontFilePath);
    if (isInit == false)
    {
        ESP_LOGE(TAG, "字库初始化失败");
        return;
    }

    String strUnicode = getUnicodeFromUTF8(str);
    String codeData = getCodeDataFromFile(strUnicode);
    singleStrPixsAmount = font_size * font_size;

    int px = x;
    int py = y;
    for (uint16_t l = 0; l < strUnicode.length() / 4; l++)
    {
        String childUnicode = strUnicode.substring(4 * l, 4 * (l + 1));
        String childCodeData = codeData.substring(font_page * l, font_page * (l + 1));
        String childPixData = getPixDataFromHex(childCodeData);
        int f = 0;
        sscanf(childUnicode.c_str(), "%x", &f);
        u_int sep = 1; // 字间距

        if (f <= 127) // ANSI 字符
        {
            if (f == 13 || f == 10) // \r\n
            {
                px = 0;
                py += font_size + sep;
                continue;
            }
            else if (f == 9) // \t
            {
                px += font_size + sep;
                continue;
            }

            if (backColor == -1)
                DrawSingleStr(px, py, childPixData, fontColor, true);
            else
                DrawSingleStr(px, py, childPixData, fontColor, backColor, true);

            px += font_size / 2 + sep;
        }
        else // 中文字符
        {
            if ((px + font_size) > screenWidth)
            {
                px = 0;
                py += font_size + sep;
            }
            if (backColor == -1)
                DrawSingleStr(px, py, childPixData, fontColor, false);
            else
                DrawSingleStr(px, py, childPixData, fontColor, backColor, false);
            px += font_size + sep;
        }
    }
}

void XFont::DrawStrEx(int x, int y, String str, int fontColor)
{
    DrawStrEx(x, y, str, fontColor, -1);
}
