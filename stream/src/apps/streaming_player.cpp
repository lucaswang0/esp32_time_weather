#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <lwip/sockets.h>
#include "../main.h"
#include "../lcd/lcd.h"
#include "../menu/menu.h"
#include "streaming_player.h"

#define SERVER_HOST "10.45.1.3"
#define HTTP_PORT 9990
#define TCP_BASE 9991
#define FRAME_SIZE (160 * 128 * 2)
#define VISIBLE_ITEMS 3
#define MAX_STREAMS 8

static struct {
    char name[32];
    int port;
    bool online;
} streamList[MAX_STREAMS];
static int streamCount = 0;
static int streamSel = 0;
static int streamScroll = 0;

enum State { ST_INIT, ST_LIST, ST_PLAYING, ST_ERROR };
static State state;
static WiFiClient tcpClient;
static unsigned long lastBtnT = 0;
static int rdPhase = 0;
static int rdPos = 0;
static uint8_t rawBuf[4 + FRAME_SIZE];
static unsigned long lastFrameT = 0;

static void clearRow(int y) {
    fill_rect(0, y, 160, 16, BLACK);
}

static void drawStreamItem(int idx, int row) {
    int y = 28 + row * 20;
    bool active = (idx == streamSel);
    fill_rect(0, y, 160, 16, BLACK);
    char buf[24];
    snprintf(buf, sizeof(buf), "%s%d. %s",
             active ? "*" : " ", idx + 1, streamList[idx].name);
    draw_str(8, y, buf, active ? WHITE : GRAY, BLACK);
}

static void drawList() {
    fill_screen(BLACK);
    draw_str_center(4, "Streaming Player", WHITE, BLACK);
    int n = streamCount;
    if (n == 0) {
        draw_str_center(50, "No streams online", GRAY, BLACK);
        draw_str_center(98, "B:Back to menu", DKGRAY, BLACK);
        return;
    }
    for (int i = 0; i < VISIBLE_ITEMS && (streamScroll + i) < n; i++)
        drawStreamItem(streamScroll + i, i);
    draw_str_center(98, "A:Play  B:Exit", DKGRAY, BLACK);
}

static void updateListSel() {
    if (streamSel < streamScroll) streamScroll = streamSel;
    if (streamSel >= streamScroll + VISIBLE_ITEMS)
        streamScroll = streamSel - VISIBLE_ITEMS + 1;
    for (int i = 0; i < VISIBLE_ITEMS && (streamScroll + i) < streamCount; i++)
        drawStreamItem(streamScroll + i, i);
}

static void showError(const char* line1, const char* line2) {
    fill_screen(BLACK);
    draw_str_center(40, line1, WHITE, BLACK);
    draw_str_center(60, line2, GRAY, BLACK);
    draw_str_center(98, "A:Retry  B:Back to menu", DKGRAY, BLACK);
}

static bool parseStreamList(const char* json) {
    streamCount = 0;
    const char* p = json;
    while (*p && *p != '[') p++;
    if (!*p) return false;
    p++;
    while (*p && streamCount < MAX_STREAMS) {
        while (*p && *p != '{') p++;
        if (!*p) break;
        p++;
        char name[32] = {0};
        int port = 0;
        bool online = true;
        while (*p && *p != '}') {
            while (*p && *p <= ' ') p++;
            if (!*p || *p == '}') break;
            if (*p != '\"') { p++; continue; }
            p++;
            char key[32];
            int ki = 0;
            while (*p && *p != '\"' && ki < 31) key[ki++] = *p++;
            key[ki] = 0;
            if (*p == '\"') p++;
            while (*p && *p <= ' ') p++;
            if (*p == ':') p++;
            while (*p && *p <= ' ') p++;
            if (*p == '\"') {
                p++;
                char val[64];
                int vi = 0;
                while (*p && *p != '\"' && vi < 63) val[vi++] = *p++;
                val[vi] = 0;
                if (*p == '\"') p++;
                if (strcmp(key, "name") == 0) {
                    strncpy(name, val, 31);
                    name[31] = 0;
                }
            } else if (*p == 't' || *p == 'f') {
                if (strcmp(key, "online") == 0) {
                    online = (*p == 't');
                }
                if (*p == 't') {
                    if (strncmp(p, "true", 4) == 0) p += 4;
                } else {
                    if (strncmp(p, "false", 5) == 0) p += 5;
                }
            } else if (*p >= '0' && *p <= '9') {
                if (strcmp(key, "port") == 0) {
                    port = 0;
                    while (*p >= '0' && *p <= '9') {
                        port = port * 10 + (*p - '0');
                        p++;
                    }
                } else {
                    while (*p >= '0' && *p <= '9') p++;
                }
            } else p++;
            while (*p && *p != ',' && *p != '}') p++;
            if (*p == ',') p++;
        }
        if (*p == '}') p++;
        if (name[0]) {
            strcpy(streamList[streamCount].name, name);
            streamList[streamCount].port = port ? port : (TCP_BASE + streamCount);
            streamList[streamCount].online = true;
            streamCount++;
        }
        while (*p && *p != ',' && *p != ']') p++;
        if (*p == ',') { p++; }
    }
    return streamCount > 0;
}

static bool fetchStreamList() {
    HTTPClient http;
    char url[96];
    snprintf(url, sizeof(url), "http://%s:%d/api/streams", SERVER_HOST, HTTP_PORT);
    http.begin(url);
    http.setTimeout(5000);
    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }
    String payload = http.getString();
    http.end();
    return parseStreamList(payload.c_str());
}

static bool initPlayback(int idx) {
    if (tcpClient.connected()) tcpClient.stop();
    Serial.printf("[STREAM] Connecting to %s:%d...\n", SERVER_HOST, streamList[idx].port);
    if (!tcpClient.connect(SERVER_HOST, streamList[idx].port, 5000)) {
        Serial.printf("[STREAM] Connect FAILED\n");
        return false;
    }
    int rcvbuf = 131072;
    setsockopt(tcpClient.fd(), SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    spi.setFrequency(80000000);
    rdPhase = 0; rdPos = 0; lastFrameT = millis();
    Serial.printf("[STREAM] Connected OK\n");
    return true;
}

static void spiWriteFrame(const uint8_t* data) {
    digitalWrite(PIN_DC, LOW); digitalWrite(PIN_CS, LOW); spi.transfer(0x2A); digitalWrite(PIN_CS, HIGH);
    digitalWrite(PIN_DC, HIGH); digitalWrite(PIN_CS, LOW);
    spi.transfer(0); spi.transfer(0); spi.transfer(0); spi.transfer(127);
    digitalWrite(PIN_CS, HIGH);
    digitalWrite(PIN_DC, LOW); digitalWrite(PIN_CS, LOW); spi.transfer(0x2B); digitalWrite(PIN_CS, HIGH);
    digitalWrite(PIN_DC, HIGH); digitalWrite(PIN_CS, LOW);
    spi.transfer(0); spi.transfer(0); spi.transfer(0); spi.transfer(159);
    digitalWrite(PIN_CS, HIGH);
    digitalWrite(PIN_DC, LOW); digitalWrite(PIN_CS, LOW); spi.transfer(0x2C); digitalWrite(PIN_CS, HIGH);
    digitalWrite(PIN_DC, HIGH); digitalWrite(PIN_CS, LOW);
    spi.writeBytes(data, FRAME_SIZE);
    digitalWrite(PIN_CS, HIGH);
}

void streaming_player_init() {
    state = ST_INIT;
    streamCount = 0;
    streamSel = 0;
    streamScroll = 0;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[STREAM] No WiFi\n");
        state = ST_ERROR;
        showError("请先在WiFi Manager", "连接网络后再使用");
        return;
    }

    fill_screen(BLACK);
    draw_str_center(40, "正在连接服务器...", WHITE, BLACK);

    if (!fetchStreamList()) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s:%d", SERVER_HOST, HTTP_PORT);
        Serial.printf("[STREAM] Fetch stream list FAILED\n");
        state = ST_ERROR;
        showError("无法连接到服务器", buf);
        return;
    }
    Serial.printf("[STREAM] Got %d streams, entering LIST\n", streamCount);
    state = ST_LIST;
    drawList();
}

void streaming_player_loop() {
    unsigned long n = millis();

    if (state == ST_PLAYING) {
        static unsigned long lastB = 0;
        if (digitalRead(12) == LOW && (n - lastB > 200)) {
            lastB = n;
            Serial.printf("[STREAM] User pressed B, exiting\n");
            tcpClient.stop();
            state = ST_LIST;
            drawList();
            return;
        }
        if (!tcpClient.connected()) {
            Serial.printf("[STREAM] Connection lost\n");
            tcpClient.stop();
            state = ST_LIST;
            drawList();
            return;
        }
        bool gotData = false;
        while (true) {
            int bufOff = (rdPhase == 0) ? rdPos : (4 + rdPos);
            int want = (rdPhase == 0) ? (4 - rdPos) : (FRAME_SIZE - rdPos);
            int r = tcpClient.read(rawBuf + bufOff, want);
            if (r <= 0) break;
            rdPos += r;
            if (rdPhase == 0 && rdPos >= 4) {
                int32_t len = ((int32_t)rawBuf[0] << 24) |
                              ((int32_t)rawBuf[1] << 16) |
                              ((int32_t)rawBuf[2] << 8) |
                              rawBuf[3];
                if (len != FRAME_SIZE) { rdPhase = 0; rdPos = 0; break; }
                rdPhase = 1; rdPos = 0;
            } else if (rdPhase == 1 && rdPos >= FRAME_SIZE) {
                spiWriteFrame(rawBuf + 4);
                rdPhase = 0; rdPos = 0;
                lastFrameT = n;
                gotData = true;
            }
        }
        if (!gotData && n - lastFrameT > 5000) {
            Serial.printf("[STREAM] RX timeout, disconnecting\n");
            tcpClient.stop();
            state = ST_LIST;
            drawList();
        }
        return;
    }

    if (n - lastBtnT < 100) return;

    bool up = digitalRead(2) == LOW;
    bool dn = digitalRead(13) == LOW;
    bool a  = digitalRead(34) == LOW;
    bool b  = digitalRead(12) == LOW;
    if (!up && !dn && !a && !b) return;
    lastBtnT = n;

    if (state == ST_LIST) {
        if (up && streamSel > 0) { streamSel--; updateListSel(); }
        else if (dn && streamSel < streamCount - 1) { streamSel++; updateListSel(); }
        else if (a && streamCount > 0) {
            if (initPlayback(streamSel)) {
                Serial.printf("[STREAM] Starting playback: %s\n", streamList[streamSel].name);
                state = ST_PLAYING;
                fill_screen(BLACK);
            } else {
                Serial.printf("[STREAM] Playback init FAILED: %s\n", streamList[streamSel].name);
                showError("无法连接到视频流", streamList[streamSel].name);
                state = ST_ERROR;
            }
        }
        else if (b) { Serial.printf("[STREAM] Back to menu from LIST\n"); show_menu(); }
    }
    else if (state == ST_ERROR) {
        if (a) {
            Serial.printf("[STREAM] Retry...\n");
            streaming_player_init();
        }
        else if (b) {
            Serial.printf("[STREAM] Back to menu from ERROR\n");
            show_menu();
        }
    }
}