#pragma once
#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <ReaderCore.h>
#include <vector>
namespace ESPConfig { class ESPConfigManager; }

struct BookFile {
  String path;
  bool sd;
  size_t size;
  String identity() const { return String(sd ? "sd:" : "flash:") + path; }
  String name() const { return path.substring(path.lastIndexOf('/') + 1); }
};
class BookInput : public reader::Input {
public:
  File file;
  bool open(const BookFile& book);
  uint32_t size() const override { return file ? const_cast<File&>(file).size() : 0; }
  bool read(uint32_t offset, void* buffer, size_t count) override;
};
bool inflateChapter(const uint8_t* source, size_t length, uint8_t* output, size_t expanded);
extern bool flashMounted, sdMounted;
extern std::vector<BookFile> library;
void mountStorage();
void scanLibrary();
bool formatFlash();

struct Location {
  uint32_t chapter, offset;
  Location(uint32_t chapter = 0, uint32_t offset = 0) : chapter(chapter), offset(offset) {}
};
struct ReadingState {
  uint32_t version = 1;
  uint32_t signature = 0;
  uint64_t identity = 0;
  Location current;
  uint32_t bookmarkCount = 0;
  Location bookmarks[32];
};
ReadingState loadReadingState(const BookFile& book, uint32_t signature);
bool saveReadingState(const ReadingState& state);
bool readingGuideSeen();
void markReadingGuideSeen();

struct ReaderSettings {
  int fontSize = 24;
  int lineGap = 2;
  int sleepSeconds = 8;
  int fullRefresh = 8;
  int wifiTimeout = 300;
  bool inverted = false;
  bool uploadAuth = true;
  bool sounds = true;
  String ssid, wifiPassword, uploadPassword;
};
extern ReaderSettings settings;
extern bool settingsChanged, requestWifi, requestLibrary;
void beginSettings();
void pollSettings();
void changeFontSize(int size);
void changeLineGap(int gap);
void changeInverted(bool inverted);
void changeUploadAuth(bool required);
void changeSounds(bool enabled);
void copySettingsTo(ESPConfig::ESPConfigManager& registry);
void applyRemoteSetting(const String& key, const String& value);
int internalFileList(const String& directory, size_t offset, String& result);

extern bool wifiActive, uploadChanged;
extern String wifiMessage;
bool startWifi();
void stopWifi();
void pollWifi();
String wifiAddress();
String wifiNetwork();
String wifiPasscode();
bool wifiLoginRequired();
