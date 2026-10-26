#include "BleAccess.h"
#include "BleTransfer.h"
#include "FileAccess.h"
#include "Services.h"
#include <ESPConfig.h>
#include <ESPConfigNimBLEInterface.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <memory>

bool bleActive = false;
String bleMessage;
namespace {
enum class Kind : uint8_t { Command, Data, Setting, Wifi, Library };
struct Event { Kind kind; uint32_t epoch; uint16_t length, mtu; uint8_t data[512]; };
QueueHandle_t events = nullptr;
std::atomic<uint32_t> epoch{0}, lastActivity{0};
std::atomic<bool> connected{false}, overflow{false};
uint32_t observedEpoch = 0;
std::unique_ptr<ESPConfig::ESPConfigManager> bleSettings;
std::unique_ptr<ESPConfig::ESPConfigNimBLEInterface> configBle;
NimBLECharacteristic *response = nullptr, *binary = nullptr;
String password;
std::string commands, outgoing;
size_t outgoingOffset = 0, maxPacket = 20;
uint8_t pendingBinary[184];
size_t pendingBinaryLength = 0;
bool authorized = false;
unsigned authFailures = 0;
uint32_t authBlockedUntil = 0;
File uploadFile, downloadFile;
String uploadName;
uint32_t uploadSize = 0, uploadOffset = 0, uploadCrc = 0xffffffff;
uint32_t lastChunkOffset = 0, lastChunkCrc = 0;
size_t lastChunkLength = 0;
const char* staging = "/books/.ble-upload.part";

void enqueue(const Event& event) {
  lastActivity.store(millis());
  if (events && xQueueSend(events, &event, 0) != pdTRUE) overflow.store(true);
}
void abortFiles() {
  uploadFile.close(); downloadFile.close();
  if (flashMounted) LittleFS.remove(staging);
  uploadOffset = uploadSize = 0; lastChunkLength = 0;
}
std::string quote(const String& s) { return fileaccess::jsonString(std::string(s.c_str(), s.length())); }
void reply(const char* op, const std::string& extra = "") {
  outgoing = "{\"op\":" + fileaccess::jsonString(op) + ",\"ok\":true" + extra + "}\n";
  outgoingOffset = 0;
}
void error(const char* message) {
  outgoing = "{\"op\":\"error\",\"ok\":false,\"message\":" + fileaccess::jsonString(message) + "}\n";
  outgoingOffset = 0;
}
const char* stringField(cJSON* obj, const char* key) {
  auto* value = cJSON_GetObjectItemCaseSensitive(obj, key);
  return cJSON_IsString(value) ? value->valuestring : nullptr;
}
bool numberField(cJSON* obj, const char* key, uint32_t& result) {
  auto* value = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) || value->valuedouble < 0 ||
      value->valuedouble > UINT32_MAX || std::floor(value->valuedouble) != value->valuedouble) return false;
  result = uint32_t(value->valuedouble); return true;
}
bool validPath(const char* path, bool root = false) { return path && fileaccess::publicPath(path, root); }

class WriteCallbacks : public NimBLECharacteristicCallbacks {
  bool data_;
public:
  explicit WriteCallbacks(bool data) : data_(data) {}
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
    const auto value = characteristic->getValue();
    if (value.size() > sizeof(Event::data)) { overflow.store(true); return; }
    Event event{}; event.kind = data_ ? Kind::Data : Kind::Command;
    event.epoch = epoch.load(); event.mtu = info.getMTU(); event.length = value.size();
    memcpy(event.data, value.data(), value.size()); enqueue(event);
  }
};
WriteCallbacks commandCallbacks(false), dataCallbacks(true);
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override {
    epoch.fetch_add(1); connected.store(true); lastActivity.store(millis());
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    connected.store(false); epoch.fetch_add(1); lastActivity.store(millis());
    // ESPConfig's connection handle can be reused by NimBLE. Relock it even
    // when the next client does not initiate a normal HELLO handshake.
    if (configBle) {
      configBle->setPassword(password);
      configBle->getResponseCharacteristic()->setValue("");
    }
    if (response) response->setValue("");
    if (binary) binary->setValue("");
  }
};
ServerCallbacks serverCallbacks;

void handleCommand(const std::string& command) {
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> obj(cJSON_ParseWithOpts(command.c_str(), nullptr, true), cJSON_Delete);
  if (!obj || !cJSON_IsObject(obj.get())) { error("Invalid command JSON"); return; }
  const char* op = stringField(obj.get(), "op");
  if (!op) { error("Missing command"); return; }
  if (!strcmp(op, "hello")) {
    reply("hello", ",\"name\":" + quote(bleName()) + ",\"chunk\":" + std::to_string(maxPacket - 4) +
          ",\"protected\":true,\"authenticated\":" + (authorized ? "true" : "false")); return;
  }
  if (!strcmp(op, "auth")) {
    if (authBlockedUntil && int32_t(authBlockedUntil - millis()) > 0) { error("Please wait before trying the access code again"); return; }
    const char* supplied = stringField(obj.get(), "password");
    authorized = supplied && password == supplied;
    if (!authorized) {
      ++authFailures; authBlockedUntil = millis() + std::min(30000u, authFailures * 1000u);
      error("Incorrect BLE access code"); return;
    }
    authFailures = 0; authBlockedUntil = 0; reply("auth"); return;
  }
  if (!authorized) { error("Sign in with the BLE access code first"); return; }
  if (!strcmp(op, "storage")) {
    size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
    reply("storage", ",\"total\":" + std::to_string(total) + ",\"used\":" + std::to_string(used) +
          ",\"free\":" + std::to_string(total > used ? total - used : 0) + ",\"reserve\":65536"); return;
  }
  if (!strcmp(op, "cancel")) { abortFiles(); reply("cancel"); return; }
  if (!strcmp(op, "status")) { reply("status", ",\"offset\":" + std::to_string(uploadOffset)); return; }
  if (!flashMounted) { error("Initialize internal storage on the reader first"); return; }
  if (!strcmp(op, "list")) {
    const char* dir = stringField(obj.get(), "dir");
    uint32_t offset = 0;
    if (!numberField(obj.get(), "offset", offset) || offset > 99999 || !validPath(dir, true)) { error("Invalid folder or offset"); return; }
    String result; int status = internalFileList(dir, offset, result);
    if (status != 200) error(result.c_str());
    else reply("list", ",\"data\":" + std::string(result.c_str()));
    return;
  }
  if (!strcmp(op, "upload")) {
    if (!flashMounted) { error("Initialize internal storage first"); return; }
    const char* name = stringField(obj.get(), "name"); uint32_t size;
    if (!name || !fileaccess::uploadFilename(name) || !numberField(obj.get(), "size", size) || size < 22) {
      error("Choose an EPUB with a plain filename (maximum 96 bytes)"); return;
    }
    if (LittleFS.exists(String("/books/") + name)) { error("A book with that filename already exists"); return; }
    if (uint64_t(size) + 65536 > LittleFS.totalBytes() - LittleFS.usedBytes()) { error("Not enough internal storage"); return; }
    abortFiles(); uploadName = name; uploadSize = size; uploadCrc = 0xffffffff;
    uploadFile = LittleFS.open(staging, FILE_WRITE);
    if (!uploadFile) { abortFiles(); error("Cannot create upload file"); return; }
    reply("upload", ",\"offset\":0,\"chunk\":" + std::to_string(maxPacket - 4)); return;
  }
  if (!strcmp(op, "finish")) {
    uint32_t crc;
    if (!uploadFile || uploadOffset != uploadSize || !numberField(obj.get(), "crc", crc) || crc != (uploadCrc ^ 0xffffffffu)) {
      abortFiles(); error("Incomplete upload or checksum mismatch"); return;
    }
    uploadFile.close();
    BookInput input; reader::Epub epub; std::string why;
    bool valid = input.open({staging, false, uploadSize}) && epub.open(input, inflateChapter, why);
    input.file.close();
    if (!valid) { abortFiles(); error(why.empty() ? "Invalid EPUB" : why.c_str()); return; }
    if (LittleFS.exists("/books/" + uploadName) || !LittleFS.rename(staging, "/books/" + uploadName)) {
      abortFiles(); error("Cannot finish upload; filename may already exist"); return;
    }
    uploadOffset = uploadSize = 0; bleMessage = "Uploaded: " + uploadName; reply("finish"); return;
  }
  if (!strcmp(op, "download")) {
    const char* path = stringField(obj.get(), "path");
    if (uploadFile) { error("Finish or cancel the current upload first"); return; }
    if (!validPath(path)) { error("Invalid file path"); return; }
    downloadFile.close(); downloadFile = LittleFS.open(path, FILE_READ);
    if (!downloadFile || downloadFile.isDirectory()) { downloadFile.close(); error("File not found"); return; }
    reply("download", ",\"size\":" + std::to_string(downloadFile.size())); return;
  }
  if (!strcmp(op, "read")) {
    uint32_t offset;
    if (!downloadFile || !numberField(obj.get(), "offset", offset) || offset > downloadFile.size()) { error("Invalid download offset"); return; }
    size_t length = std::min<size_t>(maxPacket - 4, downloadFile.size() - offset);
    if (!downloadFile.seek(offset) || (length && downloadFile.read(pendingBinary + 4, length) != length)) {
      error("Cannot read file"); return;
    }
    bletransfer::putOffset(pendingBinary, offset);
    pendingBinaryLength = length ? length + 4 : 0;
    reply("read", ",\"offset\":" + std::to_string(offset) + ",\"bytes\":" + std::to_string(length) +
          ",\"eof\":" + (offset + length == downloadFile.size() ? "true" : "false")); return;
  }
  if (!strcmp(op, "delete")) {
    const char* path = stringField(obj.get(), "path");
    if (uploadFile) { error("Finish or cancel the current upload first"); return; }
    if (!validPath(path)) { error("Invalid file path"); return; }
    downloadFile.close(); File file = LittleFS.open(path, FILE_READ);
    if (!file || file.isDirectory()) { error("File not found"); return; }
    file.close();
    if (!LittleFS.remove(path)) { error("Could not delete file"); return; }
    bleMessage = "Deleted a file"; reply("delete"); return;
  }
  error("Unknown command");
}
void handleData(const Event& event) {
  if (!authorized || !uploadFile || event.length <= 4) { error("No authorized upload is open"); return; }
  const uint32_t offset = bletransfer::offset(event.data);
  const size_t length = event.length - 4;
  uint32_t chunkCrc = bletransfer::updateCrc(0xffffffff, event.data + 4, length);
  if (offset == lastChunkOffset && length == lastChunkLength && chunkCrc == lastChunkCrc && offset + length == uploadOffset) {
    reply("chunk", ",\"offset\":" + std::to_string(uploadOffset)); return; // Idempotent retry.
  }
  if (offset != uploadOffset || !bletransfer::fits(offset, length, uploadSize)) { error("Unexpected upload offset"); return; }
  if (LittleFS.totalBytes() - LittleFS.usedBytes() < length + 65536 || uploadFile.write(event.data + 4, length) != length) {
    abortFiles(); error("Storage full or write failed"); return;
  }
  uploadCrc = bletransfer::updateCrc(uploadCrc, event.data + 4, length);
  uploadOffset += length; lastChunkOffset = offset; lastChunkLength = length; lastChunkCrc = chunkCrc;
  reply("chunk", ",\"offset\":" + std::to_string(uploadOffset));
}
void applySetting(const Event& event) {
  std::string value(reinterpret_cast<const char*>(event.data), event.length);
  size_t newline = value.find('\n');
  if (newline != std::string::npos) applyRemoteSetting(value.substr(0, newline).c_str(), value.substr(newline + 1).c_str());
}
} // namespace

String bleName() {
  char name[32]; snprintf(name, sizeof(name), "PaperS3-%04x", unsigned(ESP.getEfuseMac() & 0xffff)); return name;
}
String blePasscode() { return password; }
bool bleConnected() { return connected.load(); }
bool startBle() {
  if (bleActive) return true;
  events = xQueueCreate(16, sizeof(Event));
  if (!events) { bleMessage = "Not enough memory for BLE"; return false; }
  password = settings.uploadPassword;
  bleSettings.reset(new ESPConfig::ESPConfigManager);
  copySettingsTo(*bleSettings);
  bleSettings->setOnChange([](const ESPConfig::ConfigField& field) {
    String value = field.type == ESPConfig::FieldType::Integer ? String(field.intValue) :
                   field.type == ESPConfig::FieldType::Boolean ? String(field.boolValue ? "true" : "false") : field.value;
    String message = field.key + "\n" + value;
    Event event{}; event.kind = Kind::Setting; event.length = std::min<size_t>(message.length(), sizeof(event.data));
    memcpy(event.data, message.c_str(), event.length); enqueue(event);
  });
  bleSettings->addAction("wifi", "Open Wi-Fi files", [] { Event e{}; e.kind = Kind::Wifi; enqueue(e); });
  bleSettings->addAction("library", "Return to library", [] { Event e{}; e.kind = Kind::Library; enqueue(e); });
  if (!NimBLEDevice::init(bleName().c_str())) { stopBle(); bleMessage = "Cannot start Bluetooth"; return false; }
  NimBLEDevice::setMTU(247);
  auto* server = NimBLEDevice::createServer();
  if (!server) { stopBle(); bleMessage = "Cannot create BLE server"; return false; }
  server->setCallbacks(&serverCallbacks, false); server->advertiseOnDisconnect(true);
  configBle.reset(new ESPConfig::ESPConfigNimBLEInterface(*server, *bleSettings, bleName()));
  configBle->setPassword(password);
  if (!configBle->begin()) { stopBle(); bleMessage = "Cannot create BLE settings service"; return false; }
  auto* service = server->createService(FileServiceUuid);
  if (!service) { stopBle(); bleMessage = "Cannot create BLE file service"; return false; }
  auto* command = service->createCharacteristic(FileCommandUuid, NIMBLE_PROPERTY::WRITE, 512);
  response = service->createCharacteristic(FileResponseUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 512);
  binary = service->createCharacteristic(FileDataUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY, 512);
  if (!command || !response || !binary) { stopBle(); bleMessage = "Cannot create BLE file characteristics"; return false; }
  command->setCallbacks(&commandCallbacks); binary->setCallbacks(&dataCallbacks);
  if (!server->start()) { stopBle(); bleMessage = "Cannot start BLE services"; return false; }
  auto* advertising = NimBLEDevice::getAdvertising();
  // One 128-bit UUID fits legacy advertising; settings clients use optional
  // service access after choosing the same named device.
  advertising->addServiceUUID(FileServiceUuid);
  advertising->enableScanResponse(true); advertising->setName(bleName().c_str());
  if (!advertising->start()) { stopBle(); bleMessage = "Cannot advertise BLE"; return false; }
  observedEpoch = epoch.load(); lastActivity.store(millis()); bleActive = true;
  bleMessage = "Choose ESPConfig for settings or the BLE file tool for books.";
  return true;
}
void stopBle() {
  // Stop host callbacks before destroying their registry, transport, and queue.
  if (NimBLEDevice::isInitialized() && !NimBLEDevice::deinit(true)) {
    bleActive = true; bleMessage = "Could not stop Bluetooth; restart the reader."; return;
  }
  bleActive = false; connected.store(false);
  configBle.reset(); bleSettings.reset(); response = binary = nullptr;
  if (events) {
    Event e; while (xQueueReceive(events, &e, 0) == pdTRUE) if (e.kind == Kind::Setting) applySetting(e);
    vQueueDelete(events); events = nullptr;
  }
  commands.clear(); outgoing.clear(); outgoingOffset = pendingBinaryLength = 0;
  authorized = false; overflow.store(false); abortFiles();
}
void pollBle() {
  if (!bleActive) return;
  if (overflow.exchange(false)) { stopBle(); bleMessage = "BLE request queue full. Reconnect and retry."; return; }
  if (observedEpoch != epoch.load()) {
    observedEpoch = epoch.load(); authorized = false; authFailures = 0; authBlockedUntil = 0;
    commands.clear(); outgoing.clear(); outgoingOffset = pendingBinaryLength = 0; abortFiles();
    bleMessage = connected.load() ? "Connected. Enter the BLE access code in your browser." : "Disconnected. Ready to connect again.";
  }
  if (millis() - lastActivity.load() > 300000) { stopBle(); bleMessage = "BLE stopped after five minutes idle."; return; }
  if (pendingBinaryLength && connected.load()) {
    binary->setValue(pendingBinary, pendingBinaryLength);
    if (binary->notify()) pendingBinaryLength = 0;
    return;
  }
  if (outgoingOffset < outgoing.size() && connected.load()) {
    size_t length = std::min(maxPacket, outgoing.size() - outgoingOffset);
    response->setValue(reinterpret_cast<const uint8_t*>(outgoing.data() + outgoingOffset), length);
    if (response->notify()) outgoingOffset += length;
    return;
  }
  if (!outgoing.empty()) { outgoing.clear(); outgoingOffset = 0; }
  Event event{};
  if (xQueueReceive(events, &event, 0) != pdTRUE) return;
  if (event.kind == Kind::Setting) { applySetting(event); return; }
  if (event.kind == Kind::Wifi) { requestWifi = true; return; }
  if (event.kind == Kind::Library) { requestLibrary = true; return; }
  if (event.epoch != observedEpoch || !connected.load()) return;
  maxPacket = std::min<size_t>(180, event.mtu > 7 ? event.mtu - 3 : 20);
  if (event.kind == Kind::Data) { handleData(event); return; }
  commands.append(reinterpret_cast<const char*>(event.data), event.length);
  if (commands.size() > 2048) { commands.clear(); error("Command is too long"); return; }
  auto newline = commands.find('\n');
  if (newline != std::string::npos) {
    // One outstanding command at a time; clients wait for its response.
    if (newline + 1 != commands.size()) { commands.clear(); error("Wait for the previous command response"); return; }
    auto command = commands.substr(0, newline); commands.clear(); handleCommand(command);
  }
}
