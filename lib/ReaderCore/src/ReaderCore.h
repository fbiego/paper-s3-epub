#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace reader {
// File access is abstract so the same EPUB parser can be exercised on a host.
class Input {
public:
  virtual ~Input() = default;
  virtual uint32_t size() const = 0;
  virtual bool read(uint32_t offset, void* buffer, size_t count) = 0;
};
using Inflate = std::function<bool(const uint8_t*, size_t, uint8_t*, size_t)>;
uint32_t crc32(const void* data, size_t length);
uint64_t identityHash(const std::string& value);

struct Entry {
  std::string name;
  uint32_t compressed, expanded, offset, crc;
  uint16_t method, flags;
};
class Zip {
public:
  bool open(Input& input, Inflate inflate, std::string& error);
  bool extract(const std::string& name, std::string& output, std::string& error,
               size_t limit = 1024 * 1024);
  const std::vector<Entry>& entries() const { return entries_; }
  uint32_t signature() const { return signature_; }
private:
  Input* input_ = nullptr;
  Inflate inflate_;
  std::vector<Entry> entries_;
  uint32_t signature_ = 0;
};

struct NavigationTarget {
  std::string label;
  size_t chapter;
  std::string fragment;
};
using Anchors = std::map<std::string, uint32_t>;
class Epub {
public:
  bool open(Input& input, Inflate inflate, std::string& error);
  bool chapter(size_t index, std::string& text, std::string& error, Anchors* anchors = nullptr);
  std::vector<uint32_t> pageBreakOffsets(size_t chapter, const Anchors& anchors) const;
  bool printedPageOffset(size_t page, const Anchors& anchors, uint32_t& offset) const;
  bool chapterOffset(size_t chapter, const Anchors& anchors, uint32_t& offset) const;
  std::string title;
  std::vector<std::string> spine;
  std::vector<NavigationTarget> printedPages;
  std::vector<NavigationTarget> chapters;
  bool hasToc = false;
  uint32_t signature() const { return zip_.signature(); }
private:
  Zip zip_;
  void readNavigation(const std::string& path, bool ncx, bool toc);
};

// UTF-8 byte offsets remain stable when font size or page layout changes.
struct Line { uint32_t begin, end; };
struct Page { uint32_t offset; std::vector<Line> lines; };
using Measure = std::function<int(const std::string&)>;
std::vector<Page> paginate(const std::string& text, int width, size_t linesPerPage,
                           const Measure& measure, const std::vector<uint32_t>& breaks = {});
size_t pageForOffset(const std::vector<Page>& pages, uint32_t offset);
// Zero counts mean an unindexed chapter. Book page numbers are one-based;
// resolved chapter and local page indexes are zero-based.
uint32_t totalPages(const std::vector<uint32_t>& chapterCounts);
bool resolveBookPage(const std::vector<uint32_t>& chapterCounts, uint32_t number,
                     size_t& chapter, size_t& page);
std::string normalizedPath(const std::string& base, const std::string& href);
std::string decodeEntities(const std::string& text);
} // namespace reader
