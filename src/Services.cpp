#include "Services.h"
#include "UploadPage.h"
#include "WebLogo.h"
#include "FileAccess.h"
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPConfig.h>
#include <ESPConfigSerialInterface.h>
#include <esp32s3/rom/miniz.h>
#include <algorithm>
#include <memory>

bool flashMounted = false, sdMounted = false;
std::vector<BookFile> library;
static SPIClass sdSPI(FSPI);
static Preferences prefs;
ReaderSettings settings;
bool settingsChanged = false, requestWifi = false, requestLibrary = false;
static ESPConfig::ESPConfigManager config;
static ESPConfig::ESPConfigSerialInterface configSerial(Serial, config, "PaperS3 Reader");

bool BookInput::open(const BookFile& book) {
  file.close();
  file = book.sd ? SD.open(book.path, FILE_READ) : LittleFS.open(book.path, FILE_READ);
  return file && !file.isDirectory();
}
bool BookInput::read(uint32_t offset, void* buffer, size_t count) {
  if (!file || uint64_t(offset) + count > file.size() || !file.seek(offset)) return false;
  return !count || file.read(static_cast<uint8_t*>(buffer), count) == count;
}
bool inflateChapter(const uint8_t* source, size_t length, uint8_t* output, size_t expanded) {
  // The ROM convenience wrapper places an 11 KiB decompressor on the stack,
  // which is larger than Arduino's loop task stack. Allocate it on the heap.
  std::unique_ptr<tinfl_decompressor> inflater(new (std::nothrow) tinfl_decompressor);
  if (!inflater) return false;
  tinfl_init(inflater.get());
  size_t consumed = length, written = expanded;
  auto result = tinfl_decompress(inflater.get(), source, &consumed, output, output, &written,
                                TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  return result == TINFL_STATUS_DONE && written == expanded && consumed == length;
}
void mountStorage() {
  // Never format a failed mount automatically: it may contain the user's books.
  flashMounted = LittleFS.begin(false);
  if (flashMounted) {
    LittleFS.mkdir("/books");
    LittleFS.remove("/books/.upload.part");
    LittleFS.remove("/books/.ble-upload.part");
  }
  sdSPI.begin(39, 40, 38, 47);
}
bool formatFlash() {
  // The caller must close book/upload handles before formatting.
  LittleFS.end();
  flashMounted = false;
  if (!LittleFS.format()) return false;
  flashMounted = LittleFS.begin(false);
  if (flashMounted) LittleFS.mkdir("/books");
  return flashMounted;
}
static void scan(fs::FS& fs, const String& directory, bool sd, int depth) {
  File root = fs.open(directory);
  if (!root || !root.isDirectory()) return;
  for (File file = root.openNextFile(); file; file = root.openNextFile()) {
    String path = file.path();
    String name = String(file.name());
    name = name.substring(name.lastIndexOf('/') + 1);
    if (!name.startsWith(".")) {
      if (file.isDirectory() && depth < 8) scan(fs, path, sd, depth + 1);
      else if (!file.isDirectory()) {
        String lower = path; lower.toLowerCase();
        if (lower.endsWith(".epub") && library.size() < 512) library.push_back({path, sd, file.size()});
      }
    }
    file.close();
  }
}
void scanLibrary() {
  library.clear();
  // Called only with no SD book open, so cards can be inserted between scans.
  SD.end();
  sdMounted = SD.begin(47, sdSPI, 10000000) && SD.cardType() != CARD_NONE;
  if (flashMounted) scan(LittleFS, "/", false, 0);
  if (sdMounted) scan(SD, "/", true, 0);
  std::sort(library.begin(), library.end(), [](const BookFile& a, const BookFile& b) {
    if (a.sd != b.sd) return !a.sd;
    return a.path.compareTo(b.path) < 0;
  });
}
static String stateKey(uint64_t identity) {
  char key[16];
  snprintf(key, sizeof(key), "%015llx", static_cast<unsigned long long>(identity & 0x0fffffffffffffffull));
  return key;
}
ReadingState loadReadingState(const BookFile& book, uint32_t signature) {
  ReadingState state;
  uint64_t id = reader::identityHash(book.identity().c_str());
  Preferences storage;
  if (storage.begin("reader-state", true)) {
    String key = stateKey(id);
    if (storage.getBytesLength(key.c_str()) == sizeof(state)) storage.getBytes(key.c_str(), &state, sizeof(state));
    storage.end();
  }
  if (state.version != 1 || state.identity != id || state.signature != signature || state.bookmarkCount > 32)
    state = ReadingState{};
  state.identity = id;
  state.signature = signature;
  return state;
}
bool saveReadingState(const ReadingState& state) {
  Preferences storage;
  if (!storage.begin("reader-state", false)) return false;
  bool ok = storage.putBytes(stateKey(state.identity).c_str(), &state, sizeof(state)) == sizeof(state);
  storage.end();
  return ok;
}
bool readingGuideSeen() { return prefs.getBool("guideSeen", false); }
void markReadingGuideSeen() { prefs.putBool("guideSeen", true); }

static bool validAccessCode(const String& value) {
  if (value.length() != 6) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
  }
  return true;
}

void beginSettings() {
  prefs.begin("reader-config", false);
  settings.fontSize = constrain(prefs.getInt("font", 24), 18, 36);
  settings.lineGap = constrain(prefs.getInt("linegap", 2), -6, 16);
  settings.sleepSeconds = constrain(prefs.getInt("sleep", 8), 0, 120);
  settings.fullRefresh = constrain(prefs.getInt("refresh", 8), 1, 30);
  settings.wifiTimeout = constrain(prefs.getInt("timeout", 300), 60, 900);
  settings.inverted = prefs.getBool("inverted", false);
  settings.uploadAuth = prefs.getBool("uploadauth", true);
  settings.sounds = prefs.getBool("sounds", true);
  settings.ssid = prefs.getString("ssid", "");
  settings.wifiPassword = prefs.getString("wifipass", "");
  settings.uploadPassword = prefs.getString("uploadpass", "");
  // Migrate older passwords once, then retain the six-digit code across boots.
  if (!validAccessCode(settings.uploadPassword)) {
    char code[7]; snprintf(code, sizeof(code), "%06lu", (unsigned long)(esp_random() % 1000000));
    settings.uploadPassword = code;
    prefs.putString("uploadpass", settings.uploadPassword);
  }
  config.addInteger("font", "Reading font size", settings.fontSize);
  config.setSlider("font", 18, 36, 1);
  config.addInteger("linegap", "Line spacing offset (pixels)", settings.lineGap);
  config.setSlider("linegap", -6, 16, 1);
  config.addInteger("sleep", "Idle seconds before light sleep (0 = off)", settings.sleepSeconds);
  config.setSlider("sleep", 0, 120, 1);
  config.addInteger("refresh", "Full refresh every N pages", settings.fullRefresh);
  config.setSlider("refresh", 1, 30, 1);
  config.addInteger("timeout", "Wi-Fi auto-off seconds", settings.wifiTimeout);
  config.setSlider("timeout", 60, 900, 1);
  config.addBoolean("inverted", "Inverted display (white on black)", settings.inverted);
  config.setSwitch("inverted");
  config.addBoolean("uploadauth", "Require sign-in for Wi-Fi file access", settings.uploadAuth);
  config.setSwitch("uploadauth");
  config.addBoolean("sounds", "Button and power sounds", settings.sounds);
  config.setSwitch("sounds");
  config.addString("ssid", "Wi-Fi network for uploads", settings.ssid, false,
    [](const String& s) { return s.length() <= 32; });
  config.addString("wifipass", "Wi-Fi network password", settings.wifiPassword, false,
    [](const String& s) { return s.length() <= 63; });
  config.addString("uploadpass", "Wi-Fi / BLE access code (6 digits)", settings.uploadPassword, true,
    validAccessCode);
  config.setOnChange([](const ESPConfig::ConfigField& field) {
    if (field.key == "font") { settings.fontSize = field.intValue; prefs.putInt("font", field.intValue); }
    if (field.key == "linegap") { settings.lineGap = field.intValue; prefs.putInt("linegap", field.intValue); }
    if (field.key == "sleep") { settings.sleepSeconds = field.intValue; prefs.putInt("sleep", field.intValue); }
    if (field.key == "refresh") { settings.fullRefresh = field.intValue; prefs.putInt("refresh", field.intValue); }
    if (field.key == "timeout") { settings.wifiTimeout = field.intValue; prefs.putInt("timeout", field.intValue); }
    if (field.key == "inverted") { settings.inverted = field.boolValue; prefs.putBool("inverted", field.boolValue); }
    if (field.key == "uploadauth") { settings.uploadAuth = field.boolValue; prefs.putBool("uploadauth", field.boolValue); }
    if (field.key == "sounds") { settings.sounds = field.boolValue; prefs.putBool("sounds", field.boolValue); }
    if (field.key == "ssid") { settings.ssid = field.value; prefs.putString("ssid", field.value); }
    if (field.key == "wifipass") { settings.wifiPassword = field.value; prefs.putString("wifipass", field.value); }
    if (field.key == "uploadpass") { settings.uploadPassword = field.value; prefs.putString("uploadpass", field.value); }
    settingsChanged = true;
  });
  config.addAction("wifi", "Open Wi-Fi uploads", [] { requestWifi = true; });
  config.addAction("library", "Open / rescan library", [] { requestLibrary = true; });
  configSerial.begin();
}
void pollSettings() { configSerial.update(); }

bool wifiActive = false, uploadChanged = false;
String wifiMessage;
static WebServer server(80);
static File upload;
static bool uploadInProgress = false, uploadAuthorized = false;
static int uploadStatus = 400;
static String uploadName, uploadError, activePassword, activeSsid, fileToken;
static bool activeAuthRequired = true;
static uint32_t uploadBytes = 0, wifiLastActivity = 0;
static uint32_t wifiConnectStarted = 0;
static bool wifiServerStarted = false;
static unsigned uploadFileCount = 0;
static const char* stagingPath = "/books/.upload.part";
String wifiPasscode() { return activePassword; }
bool wifiLoginRequired() { return wifiActive ? activeAuthRequired : settings.uploadAuth; }
String wifiNetwork() {
  return wifiActive ? activeSsid : settings.ssid;
}
String wifiAddress() {
  if (!wifiActive || WiFi.status() != WL_CONNECTED) return "";
  return "http://" + WiFi.localIP().toString();
}
static bool authenticated() { return !activeAuthRequired || server.authenticate("reader", activePassword.c_str()); }
static bool requireAuth() {
  if (authenticated()) { wifiLastActivity = millis(); return true; }
  server.requestAuthentication();
  return false;
}
static bool requireFileToken() {
  if (fileToken.length() && server.header("X-Reader-Token") == fileToken) return true;
  server.send(403, "text/plain", "File session changed. Reload this page and try again.");
  return false;
}
static void uploadFailure(int status, const String& error) {
  uploadStatus = status; uploadError = error;
  upload.close();
  LittleFS.remove(stagingPath);
}
static bool publicPath(const String& path, bool allowRoot = false) {
  return fileaccess::publicPath(std::string(path.c_str(), path.length()), allowRoot);
}
static String jsonString(const String& value) {
  return fileaccess::jsonString(std::string(value.c_str(), value.length())).c_str();
}
int internalFileList(const String& path, size_t offset, String& result) {
  if (!publicPath(path, true)) { result = "Invalid folder path"; return 400; }
  size_t seen = 0, returned = 0;
  File directory = LittleFS.open(path, FILE_READ);
  if (!directory || !directory.isDirectory()) { result = "Folder not found"; return 404; }
  String json = "{\"directory\":" + jsonString(path) + ",\"files\":[";
  bool more = false;
  for (File file = directory.openNextFile(); file; file = directory.openNextFile()) {
    String fullPath = file.path();
    if (!publicPath(fullPath)) { file.close(); continue; }
    if (seen++ < offset) { file.close(); continue; }
    if (returned == 32) { more = true; file.close(); break; }
    if (returned++) json += ',';
    String name = fullPath.substring(fullPath.lastIndexOf('/') + 1);
    json += "{\"name\":" + jsonString(name) + ",\"path\":" + jsonString(fullPath) +
            ",\"size\":" + String(file.isDirectory() ? 0 : file.size()) +
            ",\"directory\":" + (file.isDirectory() ? "true" : "false") + "}";
    file.close();
  }
  directory.close();
  result = json + "],\"more\":" + String(more ? "true" : "false") + ",\"offset\":" + String(offset) +
           ",\"nextOffset\":" + String(offset + returned) + "}";
  return 200;
}
static void listFiles() {
  if (!requireAuth()) return;
  String path = server.hasArg("dir") ? server.arg("dir") : "/books";
  String offsetText = server.hasArg("offset") ? server.arg("offset") : "0";
  bool valid = offsetText.length() > 0 && offsetText.length() <= 5;
  for (unsigned i = 0; i < offsetText.length(); ++i) if (offsetText[i] < '0' || offsetText[i] > '9') valid = false;
  if (!valid) { server.send(400, "text/plain", "Invalid listing offset"); return; }
  String json;
  int status = internalFileList(path, offsetText.toInt(), json);
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, status == 200 ? "application/json; charset=utf-8" : "text/plain", json);
}
static void downloadFile() {
  if (!requireAuth()) return;
  String path = server.arg("path");
  if (!publicPath(path)) { server.send(400, "text/plain", "Invalid file path"); return; }
  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.isDirectory()) { server.send(404, "text/plain", "File not found"); return; }
  String name = path.substring(path.lastIndexOf('/') + 1);
  server.sendHeader("Content-Disposition", String("attachment; filename*=UTF-8''") + fileaccess::encodedFilename(name.c_str()).c_str());
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(file, "application/octet-stream");
  file.close();
  wifiLastActivity = millis();
}
static void deleteFile() {
  if (!requireAuth()) return;
  // A per-session token prevents cross-site form submissions, including when
  // the user explicitly disables sign-in. It does not require a login.
  if (!requireFileToken()) return;
  String path = server.arg("path");
  if (!publicPath(path)) { server.send(400, "text/plain", "Invalid file path"); return; }
  File file = LittleFS.open(path, FILE_READ);
  if (!file || file.isDirectory()) { server.send(404, "text/plain", "File not found"); return; }
  file.close();
  if (!LittleFS.remove(path)) { server.send(500, "text/plain", "Could not delete file"); return; }
  uploadChanged = true;
  wifiMessage = "Deleted: " + path.substring(path.lastIndexOf('/') + 1);
  server.send(200, "application/json", "{\"deleted\":true}");
}
static void setupRoutes() {
  static const char* headers[] = {"X-Reader-Token"};
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, [] {
    if (!requireAuth()) return;
    server.send_P(200, "text/html; charset=utf-8", uploadPage);
  });
  server.on("/papers3-library.png", HTTP_GET, [] {
    if (!requireAuth()) return;
    server.send_P(200, "image/png", reinterpret_cast<const char*>(projectLogoStart),
                  projectLogoEnd - projectLogoStart);
  });
  server.on("/api/storage", HTTP_GET, [] {
    if (!requireAuth()) return;
    const size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
    String json = "{\"total\":" + String(total) + ",\"used\":" + String(used) +
                  ",\"free\":" + String(total > used ? total - used : 0) + ",\"reserve\":65536,\"authRequired\":" +
                  (activeAuthRequired ? "true" : "false") + ",\"token\":" + jsonString(fileToken) + "}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
  });
  server.on("/api/files", HTTP_GET, listFiles);
  server.on("/download", HTTP_GET, downloadFile);
  server.on("/api/delete", HTTP_POST, deleteFile);
  server.on("/upload", HTTP_POST, [] {
    if (!requireAuth()) return;
    if (!requireFileToken()) return;
    if (uploadInProgress) uploadFailure(400, "Incomplete upload");
    if (uploadStatus == 201) {
      if (!LittleFS.rename(stagingPath, "/books/" + uploadName)) uploadFailure(507, "Cannot finish upload");
      else { uploadChanged = true; wifiMessage = "Uploaded: " + uploadName; }
    }
    if (uploadStatus != 201) server.send(uploadStatus, "text/plain", uploadError.length() ? uploadError : "Select an EPUB file");
    else server.send(201, "text/html; charset=utf-8", "<!doctype html><html lang='en'><meta name='viewport' content='width=device-width'><title>Book added</title><body style='font-family:system-ui;background:#f5f3ec;color:#243b31;padding:10vh 8vw'><h1>Ready for your next chapter.</h1><p>Your book was added. Open the library on your reader to start reading.</p><a href='/' style='color:#22664c'>Upload another book &rarr;</a></body></html>");
    uploadInProgress = false;
    uploadFileCount = 0;
    uploadStatus = 400;
    uploadError = "";
  }, [] {
    HTTPUpload& part = server.upload();
    if (part.status == UPLOAD_FILE_START) {
      // The streaming callback runs before the POST completion handler.
      uploadAuthorized = authenticated() && fileToken.length() && server.header("X-Reader-Token") == fileToken;
      if (!uploadAuthorized) return;
      wifiLastActivity = millis();
      if (++uploadFileCount > 1) { uploadFailure(400, "Only one file per upload"); return; }
      uploadInProgress = true;
      uploadStatus = 400; uploadError = ""; uploadBytes = 0;
      uploadName = part.filename;
      String lower = uploadName; lower.toLowerCase();
      bool valid = uploadName.length() > 5 && uploadName.length() <= 96 && lower.endsWith(".epub") && uploadName[0] != '.';
      for (size_t i = 0; i < uploadName.length(); ++i) {
        unsigned char c = uploadName[i];
        if (c < 32 || c == '/' || c == '\\' || c == ':' || c == 127) valid = false;
      }
      if (!valid) { uploadFailure(400, "Use a plain .epub filename (maximum 96 bytes)"); return; }
      if (LittleFS.exists("/books/" + uploadName)) { uploadFailure(409, "A book with that filename already exists"); return; }
      upload = LittleFS.open(stagingPath, FILE_WRITE);
      if (!upload) uploadFailure(507, "Cannot create upload file");
    } else if (part.status == UPLOAD_FILE_WRITE && uploadAuthorized) {
      wifiLastActivity = millis();
      if (!upload) return;
      const size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
      if (freeBytes < part.currentSize + 65536) { uploadFailure(507, "Not enough internal storage (64 KiB reserved)"); return; }
      if (upload.write(part.buf, part.currentSize) != part.currentSize) { uploadFailure(507, "Storage write failed"); return; }
      uploadBytes += part.currentSize;
    } else if (part.status == UPLOAD_FILE_END && uploadAuthorized) {
      if (!upload) { uploadInProgress = false; return; }
      upload.close();
      BookInput input;
      reader::Epub epub;
      std::string error;
      bool valid = uploadBytes == part.totalSize && input.open({stagingPath, false, uploadBytes}) && epub.open(input, inflateChapter, error);
      input.file.close();
      if (!valid) uploadFailure(400, String("Invalid EPUB: ") + error.c_str());
      else uploadStatus = 201;
      uploadInProgress = false;
    } else if (part.status == UPLOAD_FILE_ABORTED && uploadAuthorized) {
      uploadFailure(400, "Upload interrupted"); uploadInProgress = false; uploadFileCount = 0;
    }
  });
  server.onNotFound([] { if (requireAuth()) server.send(404, "text/plain", "Not found"); });
}
bool startWifi() {
  if (wifiActive) return true;
  if (!flashMounted) { wifiMessage = "Initialize internal storage first"; return false; }
  if (!settings.ssid.length()) {
    wifiMessage = "Set your Wi-Fi network and password using ESPConfig over USB, then tap Retry.";
    return false;
  }
  activePassword = settings.uploadPassword;
  activeAuthRequired = settings.uploadAuth;
  char token[33];
  snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random(),
           (unsigned long)esp_random(), (unsigned long)esp_random());
  fileToken = token;
  activeSsid = settings.ssid;
  if (!WiFi.mode(WIFI_STA)) {
    WiFi.mode(WIFI_OFF); wifiMessage = "Cannot start Wi-Fi"; return false;
  }
  WiFi.begin(activeSsid.c_str(), settings.wifiPassword.c_str());
  static bool routesReady = false;
  if (!routesReady) { setupRoutes(); routesReady = true; }
  wifiServerStarted = false;
  wifiActive = true; wifiLastActivity = wifiConnectStarted = millis();
  wifiMessage = "Connecting to your Wi-Fi network...";
  return true;
}
void stopWifi() {
  server.stop();
  wifiServerStarted = false;
  upload.close();
  if (flashMounted) LittleFS.remove(stagingPath);
  uploadInProgress = false;
  uploadFileCount = 0; uploadStatus = 400;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiActive = false;
  fileToken = "";
}
void pollWifi() {
  if (!wifiActive) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiServerStarted) {
      stopWifi(); wifiMessage = "Wi-Fi connection lost. Check your network and tap Retry.";
    } else if (millis() - wifiConnectStarted >= 30000) {
      stopWifi(); wifiMessage = "Connection failed. Check the Wi-Fi name and password in ESPConfig, then tap Retry.";
    }
    return;
  }
  if (!wifiServerStarted) {
    server.begin(); wifiServerStarted = true;
    wifiLastActivity = millis();
    wifiMessage = "Connect your phone or computer to the same network, then open the address above.";
  }
  server.handleClient();
  if (millis() - wifiLastActivity > uint32_t(settings.wifiTimeout) * 1000) {
    stopWifi(); wifiMessage = "Wi-Fi stopped after inactivity";
  }
}

void changeFontSize(int size) { config.setFieldValue("font", String(size)); }
void changeLineGap(int gap) { config.setFieldValue("linegap", String(gap)); }
void changeInverted(bool inverted) { config.setFieldValue("inverted", inverted ? "true" : "false"); }
void changeUploadAuth(bool required) { config.setFieldValue("uploadauth", required ? "true" : "false"); }
void changeSounds(bool enabled) { config.setFieldValue("sounds", enabled ? "true" : "false"); }
void applyRemoteSetting(const String& key, const String& value) { config.setFieldValue(key, value); }
void copySettingsTo(ESPConfig::ESPConfigManager& registry) {
  for (const auto& field : config.getFields()) {
    switch (field.type) {
      case ESPConfig::FieldType::Integer: registry.addInteger(field.key, field.label, field.intValue, field.required, field.validator); break;
      case ESPConfig::FieldType::Boolean: registry.addBoolean(field.key, field.label, field.boolValue, field.required, field.validator); break;
      case ESPConfig::FieldType::String: registry.addString(field.key, field.label, field.value, field.required, field.validator); break;
      default: continue;
    }
    if (field.control == ESPConfig::ControlType::Slider) registry.setSlider(field.key, field.minimum, field.maximum, field.step);
    if (field.control == ESPConfig::ControlType::Switch) registry.setSwitch(field.key);
    if (field.immediateUpdate) registry.setImmediateUpdate(field.key);
  }
}
