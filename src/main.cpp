#include <Arduino.h>
#include <M5GFX.h>
#include <ReaderJpeg.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "Services.h"
#include "Battery.h"
#include "BleAccess.h"
#include "Sounds.h"
#include "ProjectLogo.h"
#include <algorithm>
#include <functional>

static M5GFX display;
enum class View { Library, Guide, Reading, Chapters, Jump, PrintedPages, Bookmarks, Wifi, Ble, Settings, Format, Error };
static View view = View::Library;
static View guideReturn = View::Library;
struct Button { int x, y, w, h, action; bool audible; };
static std::vector<Button> buttons;
static BookInput bookInput;
static reader::Epub epub;
static BookFile currentBook;
static ReadingState state;
static std::string chapterText;
static std::vector<reader::Page> pages;
static reader::Anchors chapterAnchors;
static std::vector<reader::Image> chapterImages;
static size_t chapterIndex = 0, pageIndex = 0, libraryPage = 0, marksPage = 0;
static bool ready = false, bookOpen = false, touchedBefore = false;
static int pressedAction = -1;
static bool pressedButton = false;
static int refreshCount = 0;
static uint32_t lastInteraction = 0;
static String errorMessage, notice;
static int readingFontSize = 0;
static int readingLineGap = -1;
static int lineHeight = 26;
static constexpr int bodyTop = 18;
static int bodyBottom() { return display.height() - 18; }
static bool readingControls = false;
static constexpr int topTapHeight = 96;
static constexpr int bottomTapHeight = 156;
static std::vector<uint32_t> chapterPageCounts;
static int indexFontSize = 0;
static int indexLineGap = -1;
static bool indexing = false;
static String jumpInput, jumpMessage;
static bool jumpPrinted = false;
static size_t printedListPage = 0;
static size_t chapterListPage = 0;
static int footerY() { return display.height() - 80; }
static int rowsPerScreen() { return (footerY() - 248) / 88; }
static int markRows() { return rowsPerScreen(); }

static void draw();
static void openLibrary();
static void openWifi();
static bool loadChapter(size_t index, uint32_t offset);
static void returnToReading();
static uint16_t foreground() { return settings.inverted ? TFT_WHITE : TFT_BLACK; }
static uint16_t background() { return settings.inverted ? TFT_BLACK : TFT_WHITE; }

static void drawProjectLogo(int x, int y, int size) {
  static_assert(sizeof(logo_56_data) == 56 * 56 / 8, "Invalid 56px logo size");
  static_assert(sizeof(logo_240_data) == 240 * 240 / 8, "Invalid 240px logo size");
  // Draw the supplied MSB-first bitmaps directly, with no decoding, scaling
  // or temporary sprite. Foreground/background follow the selected theme.
  if (size == 56) display.drawBitmap(x, y, logo_56_data, 56, 56, foreground(), background());
  else if (size == 240) display.drawBitmap(x, y, logo_240_data, 240, 240, foreground(), background());
}

static void uiFont(int size = 24, bool bold = true) {
  // DejaVu24 in M5GFX only contains ASCII. efont also covers accented text
  // and typographic punctuation commonly found in EPUBs.
  display.setFont(bold ? &fonts::efontJA_24_b : &fonts::efontJA_24);
  display.setTextSize(float(size) / 24.0f);
  display.setTextDatum(textdatum_t::top_left);
  display.setTextColor(foreground(), background());
}
static String fit(String text, int width) {
  if (display.textWidth(text) <= width) return text;
  while (text.length() && display.textWidth(text + "...") > width) {
    size_t p = text.length() - 1;
    while (p > 0 && (uint8_t(text[p]) & 0xc0) == 0x80) --p;
    text.remove(p);
  }
  return text + "...";
}
static void label(const String& text, int x, int y, int size = 24, int width = -1) {
  uiFont(size);
  display.drawString(fit(text, width < 0 ? display.width() - x - 20 : width), x, y);
}
static void button(int x, int y, int w, int h, const String& text, int action, int size = 22) {
  display.drawRoundRect(x, y, w, h, 8, foreground());
  display.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, foreground());
  uiFont(size);
  display.setTextDatum(textdatum_t::middle_center);
  display.drawString(fit(text, w - 16), x + w / 2, y + h / 2);
  buttons.push_back({x, y, w, h, action, true});
}
static void wrapped(const String& text, int y, int size = 22) {
  uiFont(size);
  std::string value(text.c_str());
  auto layout = reader::paginate(value, display.width() - 48, 100,
    [](const std::string& s) { return display.textWidth(s.c_str()); });
  if (layout.empty()) return;
  for (const auto& line : layout[0].lines) {
    if (y > footerY() - size - 10) break;
    display.drawString(value.substr(line.begin, line.end - line.begin).c_str(), 24, y);
    y += size + 10;
  }
}
static const reader::Image* currentPageImage() {
  if (view != View::Reading || pageIndex >= pages.size()) return nullptr;
  for (const auto& image : chapterImages)
    if (image.begin == pages[pageIndex].offset) return &image;
  return nullptr;
}
static void drawBookImage(const reader::Image& image) {
  std::string data, error;
  reader::ImageInfo info;
  bool drawn = false;
  if (epub.image(image, data, info, error)) {
    const int maxWidth = display.width() - 48;
    const int maxHeight = bodyBottom() - bodyTop - (image.alt.empty() ? 0 : 64);
    const float scale = std::min(1.0f, std::min(float(maxWidth) / info.width, float(maxHeight) / info.height));
    const int width = std::max(1, int(info.width * scale));
    const int height = std::max(1, int(info.height * scale));
    lgfx::LGFX_Sprite raster(&display);
    raster.setColorDepth(16);
    raster.setPsram(true);
    if (raster.createSprite(width, height)) {
      // Composite transparency against white and keep illustrations positive
      // even when the surrounding reader UI uses an inverted theme.
      raster.fillScreen(TFT_WHITE);
      const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());
      if (info.format == reader::ImageFormat::Png) {
        drawn = raster.drawPng(bytes, data.size(), 0, 0, width, height, 0, 0, scale);
        raster.releasePngMemory();
      } else {
        ReaderJpegImage jpeg{};
        char message[200];
        const size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        const size_t budget = freePsram > 512 * 1024 ? freePsram - 512 * 1024 : 0;
        drawn = readerDecodeJpeg(bytes, data.size(), width, height, budget,
          [] { delay(1); }, &jpeg, message, sizeof(message));
        if (drawn) {
          for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
              const uint8_t gray = jpeg.pixels[y * width + x];
              raster.drawPixel(x, y, uint16_t(((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3)));
            }
            if ((y & 15) == 0) delay(1);
          }
        } else error = message;
        readerFreeJpeg(&jpeg);
      }
      if (drawn) {
        static const uint8_t bayer[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
        for (int y = 0; y < height; ++y) {
          for (int x = 0; x < width; ++x) {
            const uint16_t pixel = raster.readPixel(x, y);
            const int r = ((pixel >> 11) & 31) * 255 / 31;
            const int g = ((pixel >> 5) & 63) * 255 / 63;
            const int b = (pixel & 31) * 255 / 31;
            const int luminance = (77 * r + 150 * g + 29 * b) >> 8;
            const int gray = std::min(15, (luminance * 15 + bayer[y & 3][x & 3] * 16) / 255) * 17;
            raster.drawPixel(x, y, uint16_t(((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3)));
          }
          if ((y & 15) == 0) delay(1);
        }
        raster.pushSprite((display.width() - width) / 2, bodyTop + (maxHeight - height) / 2);
      } else if (error.empty()) error = "Image could not be decoded";
    } else error = "Not enough memory to display image";
  }
  if (!drawn) {
    label("Image unavailable", 24, bodyTop + 40, 26);
    wrapped(error.c_str(), bodyTop + 100, 20);
    if (!image.alt.empty()) wrapped(image.alt.c_str(), bodyTop + 260, 22);
  } else if (!image.alt.empty()) {
    uiFont(18, false);
    auto caption = reader::paginate(image.alt, display.width() - 48, 2,
      [](const std::string& text) { return display.textWidth(text.c_str()); });
    if (!caption.empty()) {
      int y = bodyBottom() - 52;
      for (const auto& line : caption[0].lines) {
        display.drawString(image.alt.substr(line.begin, line.end - line.begin).c_str(), 24, y);
        y += 24;
      }
    }
  }
}
static void drawBatteryIndicator() {
  const auto& battery = batteryStatus();
  const int x = display.width() - 230, y = 80;
  display.drawRoundRect(x, y, 32, 17, 2, foreground());
  display.fillRect(x + 32, y + 5, 3, 7, foreground());
  if (battery.percent > 0) {
    int fill = std::max(1, 26 * battery.percent / 100);
    display.fillRect(x + 3, y + 3, fill, 11, foreground());
  }
  String text = battery.percent < 0 ? "--%" : "~" + String(battery.percent) + "%";
  if (battery.charging) text += " Charging";
  else if (battery.usbPowered) text += " USB";
  label(text, x + 44, 76, 18, 166);
}
static void refreshLibraryBattery() {
  if (view != View::Library || !pollBatteryStatus()) return;
  display.setEpdMode(epd_mode_t::epd_text);
  display.startWrite();
  display.fillRect(display.width() - 234, 72, 234, 30, background());
  drawBatteryIndicator();
  display.endWrite(); display.waitDisplay();
  // Do not reset lastInteraction: status updates must not prevent idle sleep.
}
static void showError(const String& error) { errorMessage = error; view = View::Error; draw(); }
static void persistPosition() {
  if (!bookOpen || pages.empty()) return;
  state.current = {uint32_t(chapterIndex), pages[pageIndex].offset};
  if (!saveReadingState(state)) notice = "Reading position could not be saved";
}
static void powerOff() {
  persistPosition();
  indexing = false;
  stopWifi();
  stopBle();
  if (bleActive) { showError(bleMessage); return; }
  bookInput.file.close();

  // Finish the e-paper update before sleeping; the image stays visible
  // without CPU activity or periodic refreshes.
  display.setEpdMode(epd_mode_t::epd_quality);
  display.startWrite();
  display.fillScreen(background());
  const int center = display.width() / 2;
  const int height = display.height();
  drawProjectLogo(center - 120, height / 8, 240);
  uiFont(34);
  display.setTextDatum(textdatum_t::middle_center);
  display.drawString("PaperS3 EPUB Reader", center, height / 2 - 65);
  uiFont(24);
  display.setTextDatum(textdatum_t::middle_center);
  display.drawString("by fbiego", center, height / 2 - 20);
  uiFont(21);
  display.setTextDatum(textdatum_t::middle_center);
  display.drawString("Your books, always within reach.", center, height / 2 + 60);
  display.drawString("An e-paper reader for EPUB books", center, height / 2 + 96);
  display.drawString("from internal storage or SD card.", center, height / 2 + 128);
  uiFont(30);
  display.setTextDatum(textdatum_t::middle_center);
  display.drawString("Device is off", center, height - 240);
  uiFont(22);
  display.setTextDatum(textdatum_t::middle_center);
  display.drawString("Press the side on/reset button", center, height - 166);
  display.drawString("to turn on.", center, height - 130);
  display.endWrite();
  display.waitDisplay();

  playPowerdownSound();
  if (display.touch()) display.touch()->sleep();
  display.sleep();
  // Off is reset-only: remove the timer and touch wake sources used while
  // reading. The hardware on/reset button starts setup() again.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_disable(GPIO_NUM_48);
  esp_deep_sleep_start();
}
static std::vector<reader::Page> layoutChapter(const std::string& text, size_t chapter,
                                             const reader::Anchors& anchors,
                                             const std::vector<reader::Image>& images) {
  uiFont(settings.fontSize, false);
  lineHeight = settings.fontSize + settings.lineGap;
  return reader::paginateIllustrated(text, display.width() - 48, (bodyBottom() - bodyTop) / lineHeight,
    [](const std::string& s) {
      static uint32_t checks = 0;
      if ((++checks & 127) == 0) delay(1);
      return display.textWidth(s.c_str());
    }, epub.pageBreakOffsets(chapter, anchors), images);
}
static void preparePageIndex() {
  if (indexFontSize != settings.fontSize || indexLineGap != settings.lineGap || chapterPageCounts.size() != epub.spine.size()) {
    chapterPageCounts.assign(epub.spine.size(), 0);
    indexFontSize = settings.fontSize;
    indexLineGap = settings.lineGap;
  }
}
static bool loadChapter(size_t index, uint32_t offset) {
  chapterText.clear(); chapterImages.clear(); chapterText.shrink_to_fit(); pages.clear();
  std::string text, error;
  chapterAnchors.clear();
  if (!epub.chapter(index, text, error, epub.printedPages.empty() && !epub.hasToc ? nullptr : &chapterAnchors, &chapterImages)) { showError(error.c_str()); return false; }
  auto layout = layoutChapter(text, index, chapterAnchors, chapterImages);
  if (layout.empty()) { showError("Cannot lay out this chapter"); return false; }
  chapterText = std::move(text);
  pages = std::move(layout);
  chapterIndex = index;
  pageIndex = reader::pageForOffset(pages, offset);
  readingFontSize = settings.fontSize;
  readingLineGap = settings.lineGap;
  preparePageIndex();
  chapterPageCounts[index] = pages.size();
  return true;
}
static bool openBook(const BookFile& file) {
  bookInput.file.close(); bookOpen = false; pages.clear(); chapterText.clear(); chapterImages.clear();
  chapterPageCounts.clear(); indexing = false; readingControls = false;
  currentBook = file;
  std::string error;
  if (!bookInput.open(file)) { showError("Cannot open book. Reinsert the SD card and rescan if needed."); return false; }
  if (!epub.open(bookInput, inflateChapter, error)) { bookInput.file.close(); showError(error.c_str()); return false; }
  state = loadReadingState(file, epub.signature());
  if (state.current.chapter >= epub.spine.size()) state.current = {};
  if (!loadChapter(state.current.chapter, state.current.offset)) return false;
  bookOpen = true;
  view = View::Reading; refreshCount = 0;
  persistPosition(); draw();
  return true;
}
static void openLibrary() {
  stopBle();
  if (bleActive) return;
  indexing = false;
  persistPosition();
  bookInput.file.close(); bookOpen = false; chapterText.clear(); chapterImages.clear(); pages.clear();
  chapterAnchors.clear();
  epub = reader::Epub{};
  scanLibrary();
  size_t count = std::max(1, rowsPerScreen());
  if (libraryPage * count >= library.size()) libraryPage = 0;
  view = View::Library; draw();
}
static void openWifi() {
  stopBle();
  if (bleActive) return;
  indexing = false;
  persistPosition();
  if (!flashMounted) { showError("Internal storage is unavailable. Open Settings to initialize LittleFS."); return; }
  // File management must not delete a book while the reader holds it open.
  bookInput.file.close(); bookOpen = false;
  chapterText.clear(); chapterImages.clear(); pages.clear(); chapterAnchors.clear(); chapterPageCounts.clear();
  epub = reader::Epub{};
  startWifi(); view = View::Wifi; draw();
}
static void openBle() {
  persistPosition(); stopWifi(); indexing = false;
  bookInput.file.close(); bookOpen = false;
  chapterText.clear(); chapterImages.clear(); pages.clear(); chapterAnchors.clear(); chapterPageCounts.clear();
  epub = reader::Epub{};
  startBle(); view = View::Ble; draw();
}
static void returnToReading() {
  indexing = false;
  if ((pages.empty() || readingFontSize != settings.fontSize || readingLineGap != settings.lineGap) &&
      !loadChapter(state.current.chapter, state.current.offset)) return;
  readingControls = false;
  view = View::Reading; persistPosition(); draw();
}
static void openJump(bool printed = true) {
  persistPosition();
  preparePageIndex();
  jumpInput = ""; jumpMessage = "";
  jumpPrinted = printed && !epub.printedPages.empty();
  indexing = !jumpPrinted && !reader::totalPages(chapterPageCounts);
  // The panel retains the page while its buffers are freed for indexing.
  chapterText.clear(); chapterImages.clear(); chapterText.shrink_to_fit(); pages.clear();
  chapterAnchors.clear();
  view = View::Jump; draw();
}
static void openChapters() {
  persistPosition(); indexing = false;
  size_t current = 0;
  for (size_t i = 0; i < epub.chapters.size(); ++i) {
    const auto& entry = epub.chapters[i];
    uint32_t offset;
    if (entry.chapter < chapterIndex || (entry.chapter == chapterIndex &&
        epub.chapterOffset(i, chapterAnchors, offset) && offset <= state.current.offset)) current = i;
  }
  chapterListPage = current / rowsPerScreen();
  view = View::Chapters; draw();
}
static void jumpToChapter(size_t target) {
  if (target >= epub.chapters.size()) return;
  if (!loadChapter(epub.chapters[target].chapter, 0)) return;
  uint32_t offset;
  if (!epub.chapterOffset(target, chapterAnchors, offset) ||
      (!chapterText.empty() && offset >= chapterText.size())) {
    pages.clear(); chapterText.clear(); chapterImages.clear();
    showError("This chapter link is missing or has no text."); return;
  }
  pageIndex = reader::pageForOffset(pages, offset);
  returnToReading();
}
static void jumpToPrintedPage(size_t target) {
  if (target >= epub.printedPages.size()) return;
  if (!loadChapter(epub.printedPages[target].chapter, 0)) return;
  uint32_t offset;
  if (!epub.printedPageOffset(target, chapterAnchors, offset) ||
      (!chapterText.empty() && offset >= chapterText.size())) {
    // Keep the current reading record intact when the publisher's link is bad.
    pages.clear(); chapterText.clear(); chapterImages.clear();
    jumpMessage = "This page marker is missing or has no text.";
    view = View::Jump; draw(); return;
  }
  pageIndex = reader::pageForOffset(pages, offset);
  returnToReading();
}
static void indexNextChapter() {
  if (!indexing || view != View::Jump) return;
  auto next = std::find(chapterPageCounts.begin(), chapterPageCounts.end(), 0);
  if (next == chapterPageCounts.end()) { indexing = false; draw(); return; }
  size_t chapter = next - chapterPageCounts.begin();
  std::string text, error;
  reader::Anchors anchors;
  std::vector<reader::Image> images;
  if (!epub.chapter(chapter, text, error, epub.printedPages.empty() && !epub.hasToc ? nullptr : &anchors, &images)) {
    indexing = false;
    jumpMessage = "Cannot index chapter " + String(chapter + 1) + ": " + error.c_str();
    draw(); return;
  }
  auto layout = layoutChapter(text, chapter, anchors, images);
  chapterPageCounts[chapter] = layout.size();
  size_t remaining = std::count(chapterPageCounts.begin(), chapterPageCounts.end(), 0);
  if (!remaining) indexing = false;
  // Update progress at most ten times, rather than refresh for every chapter.
  size_t step = std::max<size_t>(1, chapterPageCounts.size() / 10);
  if (!remaining || (chapterPageCounts.size() - remaining) % step == 0) draw();
}
static bool isBookmarked() {
  if (!bookOpen || pages.empty()) return false;
  uint32_t begin = pages[pageIndex].offset;
  uint32_t end = pageIndex + 1 < pages.size() ? pages[pageIndex + 1].offset : uint32_t(chapterText.size()) + 1;
  for (uint32_t i = 0; i < state.bookmarkCount; ++i)
    if (state.bookmarks[i].chapter == chapterIndex && state.bookmarks[i].offset >= begin && state.bookmarks[i].offset < end) return true;
  return false;
}
static void toggleBookmark() {
  uint32_t begin = pages[pageIndex].offset;
  uint32_t end = pageIndex + 1 < pages.size() ? pages[pageIndex + 1].offset : uint32_t(chapterText.size()) + 1;
  for (uint32_t i = 0; i < state.bookmarkCount; ++i) {
    if (state.bookmarks[i].chapter == chapterIndex && state.bookmarks[i].offset >= begin && state.bookmarks[i].offset < end) {
      for (uint32_t j = i + 1; j < state.bookmarkCount; ++j) state.bookmarks[j - 1] = state.bookmarks[j];
      --state.bookmarkCount; persistPosition(); draw(); return;
    }
  }
  if (state.bookmarkCount >= 32) notice = "32 bookmarks saved. Remove one first.";
  else { state.bookmarks[state.bookmarkCount++] = {uint32_t(chapterIndex), begin}; persistPosition(); }
  draw();
}
static void turnPage(bool forward) {
  readingControls = false;
  if (forward) {
    if (pageIndex + 1 < pages.size()) ++pageIndex;
    else if (chapterIndex + 1 < epub.spine.size()) { if (!loadChapter(chapterIndex + 1, 0)) return; }
    else { notice = "End of book"; draw(); return; }
  } else {
    if (pageIndex) --pageIndex;
    else if (chapterIndex) {
      if (!loadChapter(chapterIndex - 1, UINT32_MAX)) return;
    } else { notice = "Start of book"; draw(); return; }
  }
  persistPosition(); ++refreshCount; draw();
}
static void draw() {
  if (!ready) return;
  static bool lastInverted = false;
  static bool lastReadingImage = false;
  const auto* image = currentPageImage();
  bool full = image || lastReadingImage || lastInverted != settings.inverted || view != View::Reading || readingControls || refreshCount % settings.fullRefresh == 0;
  lastReadingImage = image != nullptr;
  lastInverted = settings.inverted;
  display.setEpdMode(full ? epd_mode_t::epd_quality : epd_mode_t::epd_text);
  display.startWrite();
  display.fillScreen(background());
  buttons.clear();
  int w = display.width();
  int foot = footerY();
  if (view == View::Library) {
    pollBatteryStatus(true);
    drawProjectLogo(24, 12, 56);
    label("PaperS3 library", 96, 24, 26, w - 236);
    button(w - 124, 16, 104, 54, "Off", 6);
    String storage = flashMounted ? String((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024) + " KiB free" : "LittleFS not initialized";
    label(storage + (sdMounted ? " | SD ready" : " | no SD"), 24, 76, 18, w - 270);
    drawBatteryIndicator();
    label(String(library.size()) + " books - internal storage first", 24, 108, 18);
    int rows = rowsPerScreen();
    for (int row = 0; row < rows; ++row) {
      size_t i = libraryPage * rows + row;
      if (i >= library.size()) break;
      int y = 148 + row * 88;
      button(20, y, w - 40, 78, "", 100 + row);
      label(library[i].name(), 34, y + 10, 23, w - 68);
      label(String(library[i].sd ? "SD" : "Internal") + "  |  " + String(library[i].size / 1024) + " KiB", 34, y + 45, 16);
    }
    if (library.empty()) wrapped("No EPUBs yet. Open Wi-Fi to upload books to internal storage, or insert an SD card and tap Rescan.", 180);
    button(20, foot - 76, 110, 58, "Previous", 1, 18);
    button(140, foot - 76, 110, 58, "Next", 2, 18);
    button(260, foot - 76, w - 280, 58, "Rescan", 3);
    int tile = (w - 64) / 3;
    button(20, foot, tile, 60, "Wi-Fi upload", 4, 20);
    button(32 + tile, foot, tile, 60, "Bluetooth", 7, 20);
    button(44 + 2 * tile, foot, w - 64 - 2 * tile, 60, "Settings", 5, 20);
  } else if (view == View::Guide) {
    // Draw at the same physical coordinates used by the reading hit regions.
    // No scaled screen illustration or reserved header changes these bounds.
    const int bottomY = display.height() - bottomTapHeight;
    display.drawFastHLine(0, topTapHeight - 1, w, foreground());
    display.drawFastHLine(0, bottomY, w, foreground());
    display.drawFastVLine(w / 2, topTapHeight, bottomY - topTapHeight, foreground());
    uiFont(18); display.setTextDatum(textdatum_t::middle_center);
    display.drawString("READING TOUCH GUIDE", w / 2, 20);
    uiFont(26); display.setTextDatum(textdatum_t::middle_center);
    display.drawString("Tap here for buttons", w / 2, 60);
    display.drawString("Tap here for buttons", w / 2, bottomY + 32);
    const int centerY = (topTapHeight + bottomY) / 2;
    const int leftX = w / 4, rightX = 3 * w / 4;
    display.drawString("Previous", leftX, centerY);
    display.drawString("Next", rightX, centerY);
    display.fillTriangle(leftX - 24, centerY - 64, leftX + 16, centerY - 88, leftX + 16, centerY - 40, foreground());
    display.fillTriangle(rightX + 24, centerY - 64, rightX - 16, centerY - 88, rightX - 16, centerY - 40, foreground());
    uiFont(18); display.setTextDatum(textdatum_t::middle_center);
    display.drawString("Tap and release", leftX, centerY + 54);
    display.drawString("Tap and release", rightX, centerY + 54);
    display.drawString("to go back", leftX, centerY + 84);
    display.drawString("to go forward", rightX, centerY + 84);
    button(20, foot, w - 40, 60, guideReturn == View::Library ? "Open library" : "Back to settings", 1);
  } else if (view == View::Reading) {
    uiFont(settings.fontSize, false);
    // Transparent text preserves descenders when negative spacing overlaps
    // adjacent glyph boxes. The page background was already cleared above.
    display.setTextColor(foreground());
    int y = bodyTop;
    for (const auto& line : pages[pageIndex].lines) {
      display.drawString(chapterText.substr(line.begin, line.end - line.begin).c_str(), 24, y);
      y += lineHeight;
    }
    if (image) drawBookImage(*image);
    if (chapterText.empty()) label("[No text in this chapter]", 24, bodyTop + 30, 22);
    if (readingControls) {
      // Overlay controls without repaginating or moving the reading position.
      display.fillRect(0, 0, w, topTapHeight, background());
      display.fillRect(0, display.height() - bottomTapHeight, w, bottomTapHeight, background());
      button(16, 14, 128, 60, "Library", 1);
      button(156, 14, 156, 60, "Bookmarks", 2, 20);
      button(324, 14, w - 340, 60, isBookmarked() ? "Unmark" : "Bookmark", 3, 20);
      String progress = "Ch " + String(chapterIndex + 1) + "/" + String(epub.spine.size()) + " | Screen " + String(pageIndex + 1) + "/" + String(pages.size());
      uint32_t total = reader::totalPages(chapterPageCounts);
      if (total) {
        uint32_t global = pageIndex + 1;
        for (size_t i = 0; i < chapterIndex; ++i) global += chapterPageCounts[i];
        progress = "Screen " + String(global) + "/" + String(total) + " | Ch " + String(chapterIndex + 1);
      }
      std::string printedLabel;
      for (size_t i = 0; i < epub.printedPages.size(); ++i) {
        const auto& entry = epub.printedPages[i];
        uint32_t offset;
        if (entry.chapter < chapterIndex || (entry.chapter == chapterIndex &&
            epub.printedPageOffset(i, chapterAnchors, offset) && offset <= pages[pageIndex].offset)) printedLabel = entry.label;
      }
      if (!printedLabel.empty()) progress = "Print " + String(printedLabel.c_str()) + " | " + progress;
      label(notice.length() ? notice : progress, 24, foot - 60, 18);
      int controlWidth = (w - 62) / 4;
      button(16, foot, controlWidth, 60, "Go to page", 8, 20);
      button(26 + controlWidth, foot, controlWidth, 60, "Chapters", 9, 20);
      button(36 + 2 * controlWidth, foot, controlWidth, 60, "Settings", 5, 20);
      button(46 + 3 * controlWidth, foot, w - 62 - 3 * controlWidth, 60, "Hide", 7, 20);
    } else if (notice.length()) {
      display.fillRect(0, display.height() - 42, w, 42, background());
      label(notice, 24, display.height() - 34, 18);
    }
    // Edge bands toggle controls; left/right turn pages only in the middle.
    // Actual buttons precede these regions so controls win hit testing.
    buttons.push_back({0, 0, w, topTapHeight, 7, false});
    buttons.push_back({0, display.height() - bottomTapHeight, w, bottomTapHeight, 7, false});
    int middleHeight = display.height() - topTapHeight - bottomTapHeight;
    buttons.push_back({0, topTapHeight, w / 2, middleHeight, 4, false});
    buttons.push_back({w / 2, topTapHeight, w - w / 2, middleHeight, 6, false});
  } else if (view == View::Chapters) {
    label("Go to chapter", 24, 24, 30);
    label(epub.hasToc ? "Choose a chapter title" : "No table of contents; reading sections", 24, 90, 20);
    for (int row = 0; row < rowsPerScreen(); ++row) {
      size_t i = chapterListPage * rowsPerScreen() + row;
      if (i >= epub.chapters.size()) break;
      button(20, 148 + row * 88, w - 40, 76,
             String(epub.chapters[i].label.c_str()), 100 + row, 23);
    }
    button(20, foot - 76, (w - 60) / 2, 58, "Previous list", 1, 20);
    button(40 + (w - 60) / 2, foot - 76, (w - 60) / 2, 58, "Next list", 2, 20);
    button(20, foot, w - 40, 60, "Back to book", 3);
  } else if (view == View::Jump) {
    label(jumpPrinted ? "Printed page" : "Screen page", 24, 24, 30, w - 184);
    if (!epub.printedPages.empty()) button(w - 146, 16, 126, 54, jumpPrinted ? "Screens" : "Printed", 5, 20);
    uint32_t total = reader::totalPages(chapterPageCounts);
    if (!jumpPrinted && (indexing || !total)) {
      if (epub.printedPages.empty()) label("No publisher page list found", 24, 100, 20);
      size_t done = chapterPageCounts.size() - std::count(chapterPageCounts.begin(), chapterPageCounts.end(), 0);
      wrapped(indexing ? "Preparing screen page numbers..." : "Screen page numbers unavailable", 160, 24);
      label("Chapters: " + String(done) + "/" + String(epub.spine.size()), 24, 260, 24);
      wrapped(jumpMessage.length() ? jumpMessage : "Screen numbers depend on your text size and spacing. You can cancel between chapters.", 340, 22);
    } else {
      label(jumpPrinted ? "Use the publisher's page label" : "Screen pages: 1 - " + String(total), 24, 100, 22);
      if (!jumpPrinted && epub.printedPages.empty()) label("No publisher page list found", 24, 129, 16);
      display.drawRoundRect(24, 155, w - 48, 74, 8, foreground());
      label(jumpInput.length() ? jumpInput : "Enter a page number", 42, 177, 28, w - 84);
      label(jumpMessage, 24, 250, 20);
      int keyWidth = (w - 72) / 3;
      for (int digit = 1; digit <= 9; ++digit) {
        int cell = digit - 1;
        button(24 + (cell % 3) * (keyWidth + 12), 310 + (cell / 3) * 108,
               keyWidth, 92, String(digit), 100 + digit, 30);
      }
      button(24, 634, keyWidth, 92, "Clear", 4, 22);
      button(36 + keyWidth, 634, keyWidth, 92, "0", 100, 30);
      button(48 + 2 * keyWidth, 634, keyWidth, 92, "Delete", 3, 22);
      if (jumpPrinted) button(20, 736, w - 40, 56, "Browse page labels (i, ii, ...)", 6, 20);
      button(20, foot - 76, w - 40, 60, "Go", 1);
    }
    button(20, foot, w - 40, 60, "Cancel / Back to book", 2, 22);
  } else if (view == View::PrintedPages) {
    label("Printed page labels", 24, 24, 30);
    label("Choose a publisher page marker", 24, 90, 20);
    for (int row = 0; row < rowsPerScreen(); ++row) {
      size_t i = printedListPage * rowsPerScreen() + row;
      if (i >= epub.printedPages.size()) break;
      button(20, 148 + row * 88, w - 40, 76,
             "Page " + String(epub.printedPages[i].label.c_str()), 100 + row, 24);
    }
    button(20, foot - 76, (w - 60) / 2, 58, "Previous list", 1, 20);
    button(40 + (w - 60) / 2, foot - 76, (w - 60) / 2, 58, "Next list", 2, 20);
    button(20, foot, w - 40, 60, "Back", 3);
  } else if (view == View::Bookmarks) {
    label("Bookmarks", 24, 24, 30);
    label("Tap a location to return to it", 24, 78, 20);
    for (int row = 0; row < markRows(); ++row) {
      size_t i = marksPage * markRows() + row;
      if (i >= state.bookmarkCount) break;
      String title = "Ch " + String(state.bookmarks[i].chapter + 1) + " | Location " + String(state.bookmarks[i].offset);
      button(20, 148 + row * 88, w - 40, 76, title, 100 + row, 20);
    }
    if (!state.bookmarkCount) wrapped("No bookmarks. Use Bookmark while reading to save a page.", 180);
    button(20, foot - 76, (w - 60) / 2, 58, "Previous list", 1, 20);
    button(40 + (w - 60) / 2, foot - 76, (w - 60) / 2, 58, "Next list", 2, 20);
    button(20, foot, w - 40, 60, "Back to book", 3);
  } else if (view == View::Wifi) {
    label("Wi-Fi book upload", 24, 24, 30);
    label(wifiActive ? "Using your network:" : "Wi-Fi is off", 24, 100);
    if (wifiActive) {
      label(wifiNetwork(), 24, 145, 26);
      label(wifiLoginRequired() ? "Access code (password):" : "Sign-in is disabled", 24, 204, 20);
      if (wifiLoginRequired()) wrapped(wifiPasscode(), 244, 24);
      label("Open in a browser:", 24, 330, 20);
      wrapped(wifiAddress().length() ? wifiAddress() : "Waiting for a connection...", 372, 23);
      label(wifiLoginRequired() ? "Login: reader" : "No sign-in required", 24, 472);
      label(wifiLoginRequired() ? "Use the access code above" : "Upload, download and manage files", 24, 515, 20);
      label("Auto-off after " + String(settings.wifiTimeout) + "s idle", 24, 572, 20);
    }
    wrapped(wifiMessage, 640, 20);
    button(20, foot - 76, (w - 60) / 2, 58, "Retry", 3);
    button(40 + (w - 60) / 2, foot - 76, (w - 60) / 2, 58, "Settings", 2);
    button(20, foot, w - 40, 60, "Stop Wi-Fi / Library", 1);
  } else if (view == View::Ble) {
    label("Bluetooth access", 24, 24, 30);
    label(bleActive ? bleName() : "Bluetooth is off", 24, 100, 26);
    if (bleActive) {
      label(bleConnected() ? "Device connected" : "Ready to connect", 24, 154, 22);
      label("BLE access code:", 24, 218, 22);
      wrapped(blePasscode(), 260, 24);
      wrapped("Settings: open fbiego.com/espconfig/ in a supported browser and choose Connect BLE.", 358, 22);
      wrapped("Books: open fbiego.com/papers3/ on your computer after starting the BLE file tool. The reader does not need Wi-Fi.", 500, 20);
    }
    wrapped(bleMessage, 650, 20);
    button(20, foot - 76, w - 40, 60, "Start / retry Bluetooth", 2, 22);
    button(20, foot, w - 40, 60, "Stop Bluetooth / Library", 1, 20);
  } else if (view == View::Settings) {
    label("Reader settings", 24, 24, 30);
    label("Text size: " + String(settings.fontSize), 24, 100);
    button(24, 150, 220, 62, "Smaller", 1);
    button(264, 150, w - 288, 62, "Larger", 2);
    label("Line height: " + String(settings.fontSize + settings.lineGap) + " px (" +
          (settings.lineGap >= 0 ? "+" : "") + String(settings.lineGap) + ")", 24, 236, 23);
    button(24, 280, 220, 60, "Tighter", 6);
    button(264, 280, w - 288, 60, "Looser", 7);
    button(24, 366, w - 48, 58, settings.inverted ? "Inverted mode: On" : "Inverted mode: Off", 8);
    button(24, 436, w - 48, 58, "Reading touch guide", 9);
    button(24, 506, w - 48, 58, settings.uploadAuth ? "Upload sign-in: On" : "Upload sign-in: Off", 10);
    button(24, 576, w - 48, 58, settings.sounds ? "Button & power sounds: On" : "Button & power sounds: Off", 11, 20);
    label("More settings: USB or Bluetooth", 24, 650, 18);
    label("fbiego.com/espconfig", 24, 676, 18);
    label("Sleep: " + String(settings.sleepSeconds) + "s | Refresh: " + String(settings.fullRefresh) + " screens", 24, 704, 14);
    button(20, foot - 152, w - 40, 60, flashMounted ? "Format LittleFS" : "Initialize internal storage", 3, 20);
    button(20, foot - 76, w - 40, 60, "Wi-Fi upload", 4);
    button(20, foot, w - 40, 60, bookOpen ? "Back to book" : "Library", 5);
  } else if (view == View::Format) {
    label(flashMounted ? "Format LittleFS?" : "Initialize LittleFS?", 24, 24, 30);
    wrapped("All books and files in internal storage will be permanently erased. SD card files, settings and bookmarks are kept. This cannot be undone.", 140, 24);
    button(20, foot - 90, w - 40, 70, "Erase internal storage", 1, 21);
    button(20, foot, w - 40, 60, "Cancel", 2);
  } else {
    label("Unable to continue", 24, 24, 30);
    wrapped(errorMessage, 140, 24);
    button(20, foot - 76, w - 40, 60, "Settings", 2);
    button(20, foot, w - 40, 60, "Library", 1);
  }
  display.endWrite();
  display.waitDisplay();
  notice = "";
  lastInteraction = millis();
}
static int hit(int x, int y, bool* audible = nullptr) {
  if (audible) *audible = false;
  for (const auto& b : buttons) if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
    if (audible) *audible = b.audible;
    return b.action;
  }
  return -1;
}
static void handleAction(int action, bool audible) {
  if (action < 0) return;
  // Enabling sounds gives immediate feedback; disabling is silent.
  if (view == View::Settings && action == 11) changeSounds(!settings.sounds);
  if (audible) playButtonClick();
  if (view == View::Library) {
    if (action >= 100) {
      size_t i = libraryPage * rowsPerScreen() + action - 100;
      if (i < library.size()) { BookFile file = library[i]; openBook(file); }
    } else if (action == 1 && libraryPage) { --libraryPage; draw(); }
    else if (action == 2 && (libraryPage + 1) * rowsPerScreen() < library.size()) { ++libraryPage; draw(); }
    else if (action == 3) openLibrary();
    else if (action == 4) openWifi();
    else if (action == 5) { view = View::Settings; draw(); }
    else if (action == 6) powerOff();
    else if (action == 7) openBle();
  } else if (view == View::Guide) {
    if (action == 1) { markReadingGuideSeen(); view = guideReturn; draw(); }
  } else if (view == View::Reading) {
    if (action == 1) openLibrary();
    if (action == 2) { marksPage = 0; view = View::Bookmarks; draw(); }
    if (action == 3) toggleBookmark();
    if (action == 4) turnPage(false);
    if (action == 5) { view = View::Settings; draw(); }
    if (action == 6) turnPage(true);
    if (action == 7) { readingControls = !readingControls; draw(); }
    if (action == 8) openJump();
    if (action == 9) openChapters();
  } else if (view == View::Chapters) {
    if (action >= 100) jumpToChapter(chapterListPage * rowsPerScreen() + action - 100);
    else if (action == 1 && chapterListPage) { --chapterListPage; draw(); }
    else if (action == 2 && (chapterListPage + 1) * rowsPerScreen() < epub.chapters.size()) { ++chapterListPage; draw(); }
    else if (action == 3) returnToReading();
  } else if (view == View::Jump) {
    if (action == 2) { returnToReading(); return; }
    if (action == 5) { openJump(!jumpPrinted); return; }
    if (action == 6 && jumpPrinted) { printedListPage = 0; view = View::PrintedPages; draw(); return; }
    if (!jumpPrinted && (indexing || !reader::totalPages(chapterPageCounts))) return;
    if (action >= 100 && action <= 109 && jumpInput.length() < 7) {
      jumpInput += char('0' + action - 100); jumpMessage = ""; draw();
    } else if (action == 3) {
      if (jumpInput.length()) jumpInput.remove(jumpInput.length() - 1);
      jumpMessage = ""; draw();
    } else if (action == 4) {
      jumpInput = ""; jumpMessage = ""; draw();
    } else if (action == 1) {
      if (jumpPrinted) {
        for (size_t i = 0; i < epub.printedPages.size(); ++i) {
          if (jumpInput.length() && epub.printedPages[i].label == jumpInput.c_str()) { jumpToPrintedPage(i); return; }
        }
        jumpMessage = "Label not found. Browse page labels."; draw(); return;
      }
      size_t chapter = 0, page = 0;
      if (!reader::resolveBookPage(chapterPageCounts, jumpInput.toInt(), chapter, page)) {
        jumpMessage = "Enter 1 - " + String(reader::totalPages(chapterPageCounts)); draw(); return;
      }
      if (!loadChapter(chapter, 0)) return;
      pageIndex = page;
      returnToReading();
    }
  } else if (view == View::PrintedPages) {
    if (action >= 100) jumpToPrintedPage(printedListPage * rowsPerScreen() + action - 100);
    else if (action == 1 && printedListPage) { --printedListPage; draw(); }
    else if (action == 2 && (printedListPage + 1) * rowsPerScreen() < epub.printedPages.size()) { ++printedListPage; draw(); }
    else if (action == 3) { view = View::Jump; draw(); }
  } else if (view == View::Bookmarks) {
    if (action >= 100) {
      size_t i = marksPage * markRows() + action - 100;
      if (i < state.bookmarkCount && loadChapter(state.bookmarks[i].chapter, state.bookmarks[i].offset)) {
        returnToReading();
      }
    } else if (action == 1 && marksPage) { --marksPage; draw(); }
    else if (action == 2 && (marksPage + 1) * markRows() < state.bookmarkCount) { ++marksPage; draw(); }
    else if (action == 3) returnToReading();
  } else if (view == View::Wifi) {
    if (action == 1) { stopWifi(); openLibrary(); }
    if (action == 2) { stopWifi(); view = View::Settings; draw(); }
    if (action == 3) { stopWifi(); openWifi(); }
  } else if (view == View::Ble) {
    if (action == 1) openLibrary();
    if (action == 2) { stopBle(); if (!bleActive) openBle(); }
  } else if (view == View::Settings) {
    if (action == 1 || action == 2) {
      // Use the same registry and persistence path as USB configuration.
      changeFontSize(constrain(settings.fontSize + (action == 1 ? -2 : 2), 18, 36));
    }
    if (action == 3) { view = View::Format; draw(); }
    if (action == 6 || action == 7) changeLineGap(constrain(settings.lineGap + (action == 6 ? -1 : 1), -6, 16));
    if (action == 8) changeInverted(!settings.inverted);
    if (action == 9) { guideReturn = View::Settings; view = View::Guide; draw(); }
    if (action == 10) changeUploadAuth(!settings.uploadAuth);
    if (action == 4) openWifi();
    if (action == 5) {
      if (bookOpen) {
        returnToReading();
      } else openLibrary();
    }
  } else if (view == View::Format) {
    if (action == 1) {
      persistPosition();
      stopWifi();
      stopBle();
      if (bleActive) return;
      indexing = false;
      bookInput.file.close(); bookOpen = false;
      chapterText.clear(); chapterImages.clear(); pages.clear(); chapterPageCounts.clear(); chapterAnchors.clear();
      epub = reader::Epub{};
      // Render a status page before the blocking format operation.
      display.setEpdMode(epd_mode_t::epd_quality);
      display.startWrite(); display.fillScreen(background());
      label("Formatting LittleFS...", 24, 140, 28);
      label("Please keep the reader powered on.", 24, 200, 20);
      display.endWrite(); display.waitDisplay();
      if (formatFlash()) openLibrary();
      else showError("LittleFS format failed. Check that the correct partition table was flashed.");
    }
    if (action == 2) { view = View::Settings; draw(); }
  } else {
    if (action == 1) openLibrary();
    if (action == 2) { view = View::Settings; draw(); }
  }
}
void setup() {
  Serial.begin(115200);
  beginSounds();
  if (!display.init()) return;
  ready = true;
  display.setRotation(0);
  // Keep large chapter/XML buffers in PSRAM; the display driver allocates
  // DMA buffers explicitly using capability-aware allocation.
  if (psramFound()) heap_caps_malloc_extmem_enable(4096);
  beginBattery();
  mountStorage();
  WiFi.mode(WIFI_OFF);
  beginSettings();
  playStartupSound();
  scanLibrary();
  // Always start at the library, including devices with the old resume=true
  // preference. Opening a book still restores its saved chapter/text offset.
  if (!readingGuideSeen()) { guideReturn = View::Library; view = View::Guide; }
  draw();
}
void loop() {
  if (!ready) { delay(100); return; }
  pollSettings();
  pollBle();
  bool wifiBefore = wifiActive;
  static String previousAddress;
  pollWifi();
  if (requestLibrary) { requestLibrary = false; stopWifi(); openLibrary(); }
  if (requestWifi) { requestWifi = false; openWifi(); }
  if (settingsChanged) {
    settingsChanged = false;
    if (bookOpen && (view == View::Jump || view == View::PrintedPages)) {
      preparePageIndex();
      indexing = !jumpPrinted && view == View::Jump && !reader::totalPages(chapterPageCounts);
      jumpInput = ""; jumpMessage = "";
    } else if (bookOpen && (readingFontSize != settings.fontSize || readingLineGap != settings.lineGap)) {
      if (loadChapter(chapterIndex, state.current.offset)) persistPosition();
    }
    draw();
  }
  if (view == View::Wifi && (uploadChanged || wifiBefore != wifiActive || previousAddress != wifiAddress())) {
    uploadChanged = false; previousAddress = wifiAddress(); draw();
  }
  static String previousBleStatus;
  static bool previousBleActive = false, previousBleConnected = false;
  if (view == View::Ble && (previousBleStatus != bleMessage || previousBleActive != bleActive || previousBleConnected != bleConnected())) {
    previousBleStatus = bleMessage; previousBleActive = bleActive; previousBleConnected = bleConnected(); draw();
  }
  int32_t x = 0, y = 0;
  bool touched = display.getTouch(&x, &y) > 0;
  if (touched) {
    lastInteraction = millis();
    if (!touchedBefore) pressedAction = hit(x, y, &pressedButton);
    else if (hit(x, y) != pressedAction) pressedAction = -1;
  }
  if (!touched && touchedBefore) {
    int action = pressedAction; pressedAction = -1;
    handleAction(action, pressedButton);
  }
  touchedBefore = touched;
  if (!touched) refreshLibraryBattery();
  // One chapter per loop leaves a chance to handle Cancel and USB settings
  // between indexing steps; normal page turns only paginate one chapter.
  if (!touched) indexNextChapter();
  if (!indexing && !bleActive && !wifiActive && !touched && !Serial && settings.sleepSeconds > 0 &&
      millis() - lastInteraction >= uint32_t(settings.sleepSeconds) * 1000) {
    // GT911 INT is active-low on GPIO48 (not an RTC pin). GPIO wake is
    // supported during light sleep. Keep touch and PSRAM powered.
    // A 100 ms timer is a fallback and keeps M5GFX's touch cache fresh.
    if (gpio_wakeup_enable(GPIO_NUM_48, GPIO_INTR_LOW_LEVEL) == ESP_OK &&
        esp_sleep_enable_gpio_wakeup() == ESP_OK &&
        esp_sleep_enable_timer_wakeup(100000) == ESP_OK) {
      esp_light_sleep_start();
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
      gpio_wakeup_disable(GPIO_NUM_48);
    } else delay(20);
  } else delay(10);
}
