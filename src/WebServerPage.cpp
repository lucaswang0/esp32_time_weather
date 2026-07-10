#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include "WebServerPage.h"
#include "DisplayManager.h"
#include "WiFiManager.h"

static WebServer* server = nullptr;
static fs::File uploadFile;

static const char* PAGE_HEAD = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<style>body{font-family:Arial,sans-serif;margin:10px;background:#222;color:#eee}a{color:#4af;text-decoration:none}"
  "h2{color:#fff;border-bottom:1px solid #555}pre{background:#111;padding:8px;border-radius:4px}"
  "input,button{padding:6px;margin:4px 0}button{background:#07c;color:#fff;border:none;border-radius:4px;cursor:pointer}"
  ".saved{color:#0a0}.del{color:#f44;font-size:12px}</style></head><body>";

static const char* PAGE_FOOT = "<hr><a href='/'>Home</a></body></html>";

static String sizeStr(uint64_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1024 * 1024) return String(bytes / 1024) + " KB";
  return String(bytes / (1024 * 1024)) + " MB";
}

static void handleRoot() {
  String html = PAGE_HEAD;
  html += "<h2>SPIFFS 文件管理器</h2>";
  
  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
    html += "<p><b>模式:</b> 热点</p>";
    html += "<p><b>AP:</b> ESP32-WIFI</p>";
    html += "<p><b>IP:</b> " + WiFi.softAPIP().toString() + "</p>";
    html += "<p><b>连接数:</b> " + String(WiFi.softAPgetStationNum()) + "</p>";
  } else {
    html += "<p><b>模式:</b> 客户端</p>";
    html += "<p><b>SSID:</b> " + WiFi.SSID() + "</p>";
    html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
  }
  
  html += "<hr><ul>";
  html += "<li><a href='/fs'>SPIFFS 文件管理</a></li>";
  html += "</ul>";
  
  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  html += "<hr><p><b>SPIFFS 信息:</b></p>";
  html += "<p>总容量: " + sizeStr(totalBytes) + "</p>";
  html += "<p>已用: " + sizeStr(usedBytes) + "</p>";
  html += "<p>剩余: " + sizeStr(totalBytes - usedBytes) + "</p>";
  
  html += PAGE_FOOT;
  server->send(200, "text/html", html);
}

static void handleFS() {
  String path = server->arg("path");
  if (path.length() == 0) path = "/";
  
  String html = PAGE_HEAD;
  html += "<h2>SPIFFS: " + path + "</h2>";
  
  if (path != "/") {
    String parent = path;
    if (parent.endsWith("/")) parent = parent.substring(0, parent.length() - 1);
    int pos = parent.lastIndexOf('/');
    if (pos <= 0) parent = "/";
    else parent = parent.substring(0, pos);
    html += "<a href='/fs?path=" + parent + "'>[返回上级]</a><br>";
  }
  
  fs::File dir = SPIFFS.open(path);
  if (!dir || !dir.isDirectory()) {
    html += "<p>不是目录</p>";
    html += PAGE_FOOT;
    server->send(200, "text/html", html);
    return;
  }
  
  fs::File f;
  while ((f = dir.openNextFile())) {
    String name = String(f.name());
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (name == ".") { f.close(); continue; }
    
    String fullPath = path;
    if (fullPath != "/") fullPath += "/";
    fullPath += name;
    
    if (f.isDirectory()) {
      html += "<a href='/fs?path=" + fullPath + "'>[目录] " + name + "/</a><br>";
    } else {
      html += "<a href='/download?path=" + fullPath + "'>" + name + "</a> ";
      html += "<span style='color:#888'>" + sizeStr(f.size()) + "</span> ";
      html += "<a class='del' href='/delete?path=" + fullPath + "' onclick=\"return confirm('确定删除 " + name + "?')\">[删除]</a>";
      html += "<br>";
    }
    f.close();
  }
  dir.close();
  
  html += "<hr><form action='/upload' method='post' enctype='multipart/form-data'>";
  html += "<input type='hidden' name='path' value='" + path + "'>";
  html += "<input type='file' name='file'><button type='submit'>上传文件</button></form>";
  
  html += PAGE_FOOT;
  server->send(200, "text/html", html);
}

static void handleDownload() {
  String path = server->arg("path");
  if (path.length() == 0) { server->send(404, "text/plain", "路径为空"); return; }
  
  fs::File f = SPIFFS.open(path);
  if (!f || f.isDirectory()) { server->send(404, "text/plain", "文件不存在"); return; }
  
  String name = path;
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);
  
  server->setContentLength(f.size());
  server->send(200, "application/octet-stream", "");
  server->streamFile(f, "application/octet-stream");
  f.close();
}

static void handleUpload() {
  HTTPUpload& up = server->upload();
  String path = server->arg("path");
  if (path.length() == 0) path = "/";
  
  if (up.status == UPLOAD_FILE_START) {
    String filename = path;
    if (filename != "/") filename += "/";
    filename += up.filename;
    uploadFile = SPIFFS.open(filename, "w");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
  }
}

static void handleUploadEnd() {
  server->sendHeader("Location", "/fs?path=" + server->arg("path"));
  server->send(303);
}

static void handleDelete() {
  String path = server->arg("path");
  if (path.length() == 0) { server->send(400, "text/plain", "路径为空"); return; }
  
  fs::File f = SPIFFS.open(path);
  if (!f) { server->send(404, "text/plain", "文件不存在"); return; }
  bool isDir = f.isDirectory();
  f.close();
  
  if (isDir) SPIFFS.rmdir(path);
  else SPIFFS.remove(path);
  
  String parent = path;
  if (parent.endsWith("/")) parent = parent.substring(0, parent.length() - 1);
  int pos = parent.lastIndexOf('/');
  if (pos <= 0) parent = "/";
  else parent = parent.substring(0, pos);
  
  server->sendHeader("Location", "/fs?path=" + parent);
  server->send(303);
}

WebServerPage::WebServerPage(DisplayManager& display, WiFiManager& wifi)
    : _display(display), _wifi(wifi), _serverStarted(false) {
}

WebServerPage::~WebServerPage() {
    if (server) {
        delete server;
        server = nullptr;
    }
}

void WebServerPage::onEnter() {
    _display.clearScreen();
    
    if (!server) {
        server = new WebServer(80);
    }
    
    server->on("/", handleRoot);
    server->on("/fs", handleFS);
    server->on("/download", handleDownload);
    server->on("/upload", HTTP_POST, handleUploadEnd, handleUpload);
    server->on("/delete", handleDelete);
    
    server->begin();
    _serverStarted = true;
    Serial.println("[WebServer] 已启动");
    
    _display.getTFT().setCursor(0, 40);
    _display.getTFT().setTextColor(COLOR_PRIMARY);
    _display.getTFT().setTextSize(2);
    
    if (_wifi.isConnected()) {
        _display.getTFT().println("WebServer 已启动");
        _display.getTFT().println("IP: " + String(_wifi.getLocalIP()));
    } else {
        _display.getTFT().println("WebServer 已启动");
        _display.getTFT().println("请连接 WiFi");
    }
    
    _display.getTFT().setTextSize(1);
    _display.getTFT().setTextColor(COLOR_GRAY_DARK);
    _display.getTFT().println("\n浏览器访问:");
    _display.getTFT().println("http://" + String(_wifi.getLocalIP()) + "/fs");
}

void WebServerPage::onExit() {
    if (_serverStarted && server) {
        server->stop();
        if (uploadFile) {
            uploadFile.close();
        }
        _serverStarted = false;
        Serial.println("[WebServer] 已停止");
    }
}

void WebServerPage::update() {
    if (_serverStarted && server) {
        server->handleClient();
    }
}