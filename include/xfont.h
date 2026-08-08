// XFont 字库显示类（从 tftziku 项目移植，改造为 SPIFFS + TFT_eSPI + 支持 sprite 画布）
// 原项目: https://github.com/.../tftziku
// 改造点: LittleFS→SPIFFS; 修默认构造委托 bug; 移除 TFT init(由 DisplayManager 接管);
//         适配 170x320 屏幕; 新增 _target 绘制目标(可为 TFT_eSprite)
#ifndef XFONT_H
#define XFONT_H

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

class XFont
{
public:
    // 默认构造：不初始化 TFT（由外部 DisplayManager 负责），只装载字库
    XFont();
    // 带参数构造：isTFT=true 时初始化 TFT（本项目不用）
    XFont(bool isTFT);

    fs::File fontFile;
    bool isInit = false;

    // 内部 TFT_eSPI 实例（保留以兼容原接口；本项目实际绘制用 _target 指向 DisplayManager 的 tft 或 sprite）
    TFT_eSPI tft = TFT_eSPI();

    void DrawStr(int x, int y, String str, int fontColor);
    void DrawStr(int x, int y, String str, int fontColor, int backColor);
    void DrawStrEx(int x, int y, String str, int fontColor);
    void DrawStrEx(int x, int y, String str, int fontColor, int backColor);

    // 在指定位置输出中文，本方法边读字库边显示
    void DrawChinese(int x, int y, String str, int fontColor);
    void DrawChinese(int x, int y, String str, int fontColor, int backColor);

    // 在指定位置输出中文,本方法是字库读完后，统一显示，视觉上显示最快
    void DrawChineseEx(int x, int y, String str, int fontColor);
    void DrawChineseEx(int x, int y, String str, int fontColor, int backColor);

    // 直接从字库获得指定字符的像素编码
    String GetPixDatasFromLib(String displayStr);

    // 测量字符串像素宽度（镜像 DrawStr 的 advance：ANSI=font_size/2+1，中文=font_size+1，
    // 跳过 \r\n 不计宽，\t 计 font_size+1）。供外部对齐/画布尺寸计算用，不绘制。
    int textWidth(String str);

    // 初始化字库
    void initZhiku(String fontPath);
    // 重新初始化新的字库
    void reInitZhiku(String fontPath);
    // 清除内存占用
    void clear(void);
    ~XFont(void);

    // 设置绘制目标：可传入 TFT_eSPI(屏幕) 或 TFT_eSprite(精灵画布)，传 nullptr 则用内部 tft
    // 注意: TFT_eSprite 继承自 TFT_eSPI，所以 TFT_eSprite* 可隐式转 TFT_eSPI*
    void setTarget(TFT_eSPI* target) { _target = target; }

    // 供 DisplayManager 外部访问: 读字号 / 临时改屏宽禁用换行
    int font_size = 0;
    int screenWidth = 320;

protected:
    void InitTFT();
    String getStringFromChars(uint8_t *bs, int l);
    String getUnicodeFromUTF8(String s);
    String getPixDataFromHex(String s);

    void DrawSingleStr(int x, int y, String strBinData, int fontColor, int backColor, bool ansiChar);
    void DrawSingleStr(int x, int y, String strBinData, int fontColor, bool ansiChar);

    String getCodeDataFromFile(String strUnicode);
    int getFontPage(int font_size, int bin_type);
    String getPixBinStrFromString(String displayStr);

    String strAllUnicodes = "";
    int unicode_begin_idx = 0;
    int font_unicode_cnt = 0;
    int total_font_cnt = 0;
    int bin_type = 64;
    int font_page = 0;

    unsigned long time_spent = 0;
    String fontFilePath = "/x.font";
    const char *s64 = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ@#*$";
    int pX = 16;
    int pY = 0;
    // DisplayManager 已初始化 TFT，这里默认 true 以跳过 DrawChinese 的检查
    bool isTftInited = true;

    // 适配本项目屏幕: setRotation(1) 后 320 宽 x 170 高
    int screenHeight = 170;

    int singleStrPixsAmount = 0;

private:
    bool chkAnsi(unsigned char c);
    // 绘制目标指针: nullptr 表示用内部 tft; 非 nullptr 时 DrawSingleStr 在该目标上绘制
    // 由 setTarget() 设置, DisplayManager 用 sprite 合成时传 TFT_eSprite*, 直绘屏幕时传 &tft
    TFT_eSPI* _target = nullptr;
};

#endif
