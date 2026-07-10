#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include "../main.h"
#include "../lcd/lcd.h"
#include "webserver.h"

static WebServer server(80);
static Preferences prefs;
static File uploadFile;

static String getNvsKey(const String& ssid) {
  uint32_t hash = 5381;
  for (int i = 0; i < ssid.length(); i++) {
    hash = ((hash << 5) + hash) + ssid[i];
  }
  char buf[9];
  snprintf(buf, sizeof(buf), "%08X", hash);
  return String(buf);
}

static const char* PAGE_HEAD = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<style>body{font-family:Arial,sans-serif;margin:10px;background:#222;color:#eee}a{color:#4af;text-decoration:none}"
  "h2{color:#fff;border-bottom:1px solid #555}pre{background:#111;padding:8px;border-radius:4px}"
  "input,button{padding:6px;margin:4px 0}button{background:#07c;color:#fff;border:none;border-radius:4px;cursor:pointer}"
  ".saved{color:#0a0}.del{color:#f44;font-size:12px}.bar{background:#444;height:10px;border-radius:5px;display:inline-block;vertical-align:middle}</style></head><body>";

static const char* PAGE_FOOT = "<hr><a href='/'>Home</a> <a href='/sd'>SD Card</a> <a href='/wifi'>WiFi</a></body></html>";

static bool sdOk() {
  if (!SD.cardSize()) {
    spi.end();
    spi.begin(18, 19, 23, -1);
    delay(10);
    return SD.begin(22, spi, 4000000);
  }
  return true;
}

static String sizeStr(uint64_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1024 * 1024) return String(bytes / 1024) + " KB";
  return String(bytes / (1024 * 1024)) + " MB";
}

static void handleRoot() {
  String html = PAGE_HEAD;
  html += "<h2>ESP32 WebServer</h2>";
  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
    html += "<p><b>Mode:</b> Hotspot</p>";
    html += "<p><b>AP:</b> ESP32-WIFI</p>";
    html += "<p><b>IP:</b> " + WiFi.softAPIP().toString() + "</p>";
    html += "<p><b>Clients:</b> " + String(WiFi.softAPgetStationNum()) + "</p>";
  } else {
    html += "<p><b>Mode:</b> Station</p>";
    html += "<p><b>Connected to:</b> " + WiFi.SSID() + "</p>";
    html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
  }
  html += "<hr><ul>";
  html += "<li><a href='/sd'>SD Card Manager</a></li>";
  html += "<li><a href='/wifi'>WiFi Password Manager</a></li>";
  html += "</ul>";
  html += PAGE_FOOT;
  server.send(200, "text/html", html);
}

static void handleSD() {
  if (!sdOk()) { server.send(200, "text/html", String(PAGE_HEAD) + "<h2>SD Error</h2><p>SD card not found</p>" + PAGE_FOOT); return; }

  String path = server.arg("path");
  if (path.length() == 0) path = "/";

  String html = PAGE_HEAD;
  html += "<h2>SD: " + path + "</h2>";

  if (path != "/") {
    String parent = path;
    if (parent.endsWith("/")) parent = parent.substring(0, parent.length() - 1);
    int pos = parent.lastIndexOf('/');
    if (pos <= 0) parent = "/";
    else parent = parent.substring(0, pos);
    html += "<a href='/sd?path=" + parent + "'>[..]</a><br>";
  }

  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) {
    html += "<p>Not a directory</p>";
    html += PAGE_FOOT;
    server.send(200, "text/html", html);
    return;
  }

  File f;
  while ((f = dir.openNextFile())) {
    String name = String(f.name());
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (name == ".") { f.close(); continue; }

    String fullPath = path;
    if (fullPath != "/") fullPath += "/";
    fullPath += name;

    if (f.isDirectory()) {
      html += "<a href='/sd?path=" + fullPath + "'>[D] " + name + "/</a>";
    } else {
      html += "<a href='/download?path=" + fullPath + "'>" + name + "</a> ";
      html += "<span style='color:#888'>" + sizeStr(f.size()) + "</span> ";
      html += "<a class='del' href='/delete?path=" + fullPath + "' onclick=\"return confirm('Delete " + name + "?')\">[del]</a>";
    }
    html += "<br>";
    f.close();
  }
  dir.close();

  html += "<hr><form action='/upload' method='post' enctype='multipart/form-data'>";
  html += "<input type='hidden' name='path' value='" + path + "'>";
  html += "<input type='file' name='file'><button type='submit'>Upload</button></form>";
  html += "<form action='/mkdir' method='get' style='display:inline'>";
  html += "<input type='hidden' name='path' value='" + path + "'>";
  html += "<input name='name' placeholder='dir name'><button type='submit'>MkDir</button></form>";

  html += PAGE_FOOT;
  server.send(200, "text/html", html);
}

static void handleDownload() {
  String path = server.arg("path");
  if (path.length() == 0) { server.send(404, "text/plain", "No path"); return; }
  if (!sdOk()) { server.send(500, "text/plain", "SD error"); return; }

  File f = SD.open(path);
  if (!f || f.isDirectory()) { server.send(404, "text/plain", "Not found"); return; }

  String name = path;
  int slash = name.lastIndexOf('/');
  if (slash >= 0) name = name.substring(slash + 1);

  server.setContentLength(f.size());
  server.send(200, "application/octet-stream", "");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

static void handleUpload() {
  if (!sdOk()) { server.send(500, "text/plain", "SD error"); return; }
  HTTPUpload& up = server.upload();
  String path = server.arg("path");
  if (path.length() == 0) path = "/";

  if (up.status == UPLOAD_FILE_START) {
    String filename = path;
    if (filename != "/") filename += "/";
    filename += up.filename;
    uploadFile = SD.open(filename, "w");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
  }
}

static void handleUploadEnd() {
  server.sendHeader("Location", "/sd?path=" + server.arg("path"));
  server.send(303);
}

static void handleDelete() {
  String path = server.arg("path");
  if (path.length() == 0) { server.send(400, "text/plain", "No path"); return; }
  if (!sdOk()) { server.send(500, "text/plain", "SD error"); return; }

  File f = SD.open(path);
  if (!f) { server.send(404, "text/plain", "Not found"); return; }
  bool isDir = f.isDirectory();
  f.close();

  if (isDir) SD.rmdir(path);
  else SD.remove(path);

  String parent = path;
  if (parent.endsWith("/")) parent = parent.substring(0, parent.length() - 1);
  int pos = parent.lastIndexOf('/');
  if (pos <= 0) parent = "/";
  else parent = parent.substring(0, pos);

  server.sendHeader("Location", "/sd?path=" + parent);
  server.send(303);
}

static void handleMkdir() {
  String path = server.arg("path");
  String name = server.arg("name");
  if (path.length() == 0) path = "/";
  if (name.length() == 0) { server.send(400, "text/plain", "No name"); return; }
  if (!sdOk()) { server.send(500, "text/plain", "SD error"); return; }

  String full = path;
  if (full != "/") full += "/";
  full += name;
  SD.mkdir(full);

  server.sendHeader("Location", "/sd?path=" + path);
  server.send(303);
}

static void handleWiFi() {
  String html = PAGE_HEAD;
  html += "<h2>WiFi Password Manager</h2>";
  html += "<form action='/wifi_save' method='post'>";
  html += "<p><b>Add Network</b></p>";
  html += "<p>SSID: <input type='text' name='ssid' required></p>";
  html += "<p>Password: <input type='password' name='pwd' required></p>";
  html += "<button type='submit'>Save</button></form>";

  html += "<h3>Saved Networks</h3><ul>";
  prefs.begin("wifi_pwd", false);
  String savedList = prefs.getString("saved_list", "");
  if (savedList.length() > 0) {
    int start = 0;
    while (true) {
      int comma = savedList.indexOf(',', start);
      String s = (comma >= 0) ? savedList.substring(start, comma) : savedList.substring(start);
      s.trim();
      if (s.length() > 0) {
        String pwd = prefs.getString(getNvsKey(s).c_str(), "");
        String masked;
        for (int j = 0; j < (int)pwd.length(); j++) masked += '*';
        html += "<li><b>" + s + "</b> = " + masked + " <a class='del' href='/wifi_forget?ssid=" + s + "'>[forget]</a></li>";
      }
      if (comma < 0) break;
      start = comma + 1;
    }
  }
  prefs.end();
  html += "</ul>";

  html += PAGE_FOOT;
  server.send(200, "text/html", html);
}

static void handleWiFiSave() {
  String ssid = server.arg("ssid");
  String pwd = server.arg("pwd");
  if (ssid.length() == 0 || pwd.length() == 0) {
    server.sendHeader("Location", "/wifi");
    server.send(303);
    return;
  }
  prefs.begin("wifi_pwd", false);
  Serial.printf("[WEB] Saving SSID: '%s' (len=%d)\n", ssid.c_str(), ssid.length());
  String key = getNvsKey(ssid);
  prefs.putString(key.c_str(), pwd);
  String savedList = prefs.getString("saved_list", "");
  if (savedList.indexOf(ssid) < 0) {
    if (savedList.length() > 0) savedList += ",";
    savedList += ssid;
    prefs.putString("saved_list", savedList);
  }
  prefs.end();
  server.sendHeader("Location", "/wifi");
  server.send(303);
}

static void handleWiFiForget() {
  String ssid = server.arg("ssid");
  if (ssid.length() > 0) {
    prefs.begin("wifi_pwd", false);
    prefs.remove(getNvsKey(ssid).c_str());
    String list = prefs.getString("saved_list", "");
    String newList;
    int start = 0;
    while (true) {
      int comma = list.indexOf(',', start);
      String s = (comma >= 0) ? list.substring(start, comma) : list.substring(start);
      s.trim();
      if (s.length() > 0 && s != ssid) {
        if (newList.length() > 0) newList += ",";
        newList += s;
      }
      if (comma < 0) break;
      start = comma + 1;
    }
    prefs.putString("saved_list", newList);
    prefs.end();
  }
  server.sendHeader("Location", "/wifi");
  server.send(303);
}

void ws_init() {
  server.on("/", handleRoot);
  server.on("/sd", handleSD);
  server.on("/download", handleDownload);
  server.on("/upload", HTTP_POST, handleUploadEnd, handleUpload);
  server.on("/delete", handleDelete);
  server.on("/mkdir", handleMkdir);
  server.on("/wifi", handleWiFi);
  server.on("/wifi_save", HTTP_POST, handleWiFiSave);
  server.on("/wifi_forget", handleWiFiForget);
  server.begin();
}

void ws_handle_client() {
  server.handleClient();
}

void ws_stop() {
  server.stop();
  if (uploadFile) { uploadFile.close(); uploadFile = File(); }
}
