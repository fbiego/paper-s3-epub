#include "ReaderCore.h"
#include <tinyxml2.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>

namespace reader {
static uint16_t u16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
static uint32_t u32(const uint8_t* p) { return u16(p) | (uint32_t(u16(p + 2)) << 16); }

uint32_t crc32(const void* data, size_t length) {
  uint32_t crc = 0xffffffff;
  const auto* p = static_cast<const uint8_t*>(data);
  while (length--) {
    crc ^= *p++;
    for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}
uint64_t identityHash(const std::string& value) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char c : value) hash = (hash ^ c) * 1099511628211ull;
  return hash;
}
static bool fail(std::string& error, const char* message) { error = message; return false; }

bool Zip::open(Input& input, Inflate inflate, std::string& error) {
  entries_.clear();
  input_ = &input;
  inflate_ = std::move(inflate);
  error.clear();
  if (input.size() < 22) return fail(error, "Not an EPUB ZIP archive");
  size_t length = std::min<uint32_t>(input.size(), 65557);
  std::vector<uint8_t> tail(length);
  if (!input.read(input.size() - length, tail.data(), length)) return fail(error, "Cannot read archive");
  int end = -1;
  for (int i = int(length) - 22; i >= 0; --i) {
    if (u32(&tail[i]) == 0x06054b50 && i + 22 + u16(&tail[i + 20]) == int(length)) { end = i; break; }
  }
  if (end < 0) return fail(error, "ZIP directory not found");
  const uint8_t* e = &tail[end];
  uint16_t count = u16(e + 10);
  uint32_t directorySize = u32(e + 12), cursor = u32(e + 16);
  uint64_t directoryEnd = uint64_t(cursor) + directorySize;
  if (u16(e + 4) || u16(e + 6) || u16(e + 8) != count || !count || count > 8192 ||
      directoryEnd > input.size() - length + end) return fail(error, "Unsupported ZIP directory (ZIP64/multi-disk/too large)");
  signature_ = input.size();
  size_t nameBytes = 0;
  for (unsigned n = 0; n < count; ++n) {
    uint8_t header[46];
    if (uint64_t(cursor) + sizeof(header) > directoryEnd || !input.read(cursor, header, sizeof(header)) ||
        u32(header) != 0x02014b50) return fail(error, "Broken ZIP directory");
    uint16_t nameLength = u16(header + 28);
    uint32_t next = cursor + 46 + nameLength + u16(header + 30) + u16(header + 32);
    if (!nameLength || nameLength > 1024 || next > directoryEnd) return fail(error, "Invalid ZIP filename");
    nameBytes += nameLength;
    if (nameBytes > 512 * 1024) return fail(error, "ZIP filenames exceed available memory");
    Entry entry;
    entry.name.resize(nameLength);
    if (!input.read(cursor + 46, &entry.name[0], nameLength)) return fail(error, "Cannot read ZIP filename");
    entry.flags = u16(header + 8); entry.method = u16(header + 10);
    entry.crc = u32(header + 16); entry.compressed = u32(header + 20);
    entry.expanded = u32(header + 24); entry.offset = u32(header + 42);
    if (entry.offset == 0xffffffff || entry.compressed == 0xffffffff || entry.expanded == 0xffffffff)
      return fail(error, "ZIP64 is not supported");
    signature_ = (signature_ * 16777619u) ^ entry.crc ^ crc32(entry.name.data(), entry.name.size());
    entries_.push_back(std::move(entry));
    cursor = next;
  }
  return true;
}

bool Zip::extract(const std::string& name, std::string& output, std::string& error, size_t limit) {
  output.clear();
  const Entry* entry = nullptr;
  for (const auto& e : entries_) if (e.name == name) { entry = &e; break; }
  if (!entry) { error = "Missing EPUB resource: " + name; return false; }
  if ((entry->flags & 1) || (entry->method != 0 && entry->method != 8))
    return fail(error, "Encrypted or unsupported ZIP compression");
  if (entry->expanded > limit || entry->compressed > 2 * 1024 * 1024)
    return fail(error, "EPUB chapter exceeds memory limit (1 MiB)");
  uint8_t header[30];
  if (!input_->read(entry->offset, header, sizeof(header)) || u32(header) != 0x04034b50)
    return fail(error, "Broken ZIP entry");
  uint64_t start = uint64_t(entry->offset) + 30 + u16(header + 26) + u16(header + 28);
  if (start + entry->compressed > input_->size()) return fail(error, "Truncated ZIP entry");
  output.resize(entry->expanded);
  if (entry->method == 0) {
    if (entry->compressed != entry->expanded || !input_->read(start, &output[0], output.size()))
      return fail(error, "Invalid stored ZIP entry");
  } else {
    std::vector<uint8_t> compressed(entry->compressed);
    if (!input_->read(start, compressed.data(), compressed.size()) ||
        !inflate_(compressed.data(), compressed.size(), reinterpret_cast<uint8_t*>(&output[0]), output.size()))
      return fail(error, "Cannot decompress EPUB resource");
  }
  if (crc32(output.data(), output.size()) != entry->crc) return fail(error, "EPUB checksum mismatch");
  return true;
}

static const char* localName(const char* name) {
  const char* colon = std::strchr(name, ':');
  return colon ? colon + 1 : name;
}
static tinyxml2::XMLElement* child(tinyxml2::XMLNode* parent, const char* name) {
  if (!parent) return nullptr;
  for (auto* e = parent->FirstChildElement(); e; e = e->NextSiblingElement())
    if (!std::strcmp(localName(e->Name()), name)) return e;
  return nullptr;
}
static bool token(const char* value, const char* wanted) {
  if (!value) return false;
  std::string list(value);
  for (char& c : list) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  return (" " + list + " ").find(std::string(" ") + wanted + " ") != std::string::npos;
}
static const char* attribute(tinyxml2::XMLElement* element, const char* name) {
  for (auto* a = element->FirstAttribute(); a; a = a->Next())
    if (!std::strcmp(localName(a->Name()), name)) return a->Value();
  return nullptr;
}
static std::string decodeUri(const std::string& path) {
  std::string decoded;
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '%' && i + 2 < path.size()) {
      char hex[3] = {path[i + 1], path[i + 2], 0}; char* stop;
      unsigned long c = std::strtoul(hex, &stop, 16);
      if (*stop == 0 && c) { decoded += char(c); i += 2; continue; }
    }
    decoded += path[i];
  }
  return decoded;
}
std::string normalizedPath(const std::string& base, const std::string& href) {
  std::string path = href;
  auto end = path.find_first_of("#?");
  if (end != std::string::npos) path.resize(end);
  // EPUB resource paths are URI references.
  std::string decoded = decodeUri(path);
  path = (!decoded.empty() && decoded[0] == '/') ? decoded.substr(1) : base + decoded;
  std::vector<std::string> parts;
  for (size_t pos = 0; pos < path.size();) {
    auto slash = path.find('/', pos);
    std::string part = path.substr(pos, slash == std::string::npos ? slash : slash - pos);
    if (part == "..") { if (parts.empty()) return ""; parts.pop_back(); }
    else if (!part.empty() && part != ".") parts.push_back(part);
    if (slash == std::string::npos) break;
    pos = slash + 1;
  }
  std::string result;
  for (const auto& p : parts) { if (!result.empty()) result += '/'; result += p; }
  return result;
}

bool Epub::open(Input& input, Inflate inflate, std::string& error) {
  title.clear(); spine.clear(); printedPages.clear(); chapters.clear(); hasToc = false;
  if (!zip_.open(input, std::move(inflate), error)) return false;
  for (const auto& e : zip_.entries())
    if (e.name == "META-INF/rights.xml") return fail(error, "DRM-protected books are not supported");
  std::string xml;
  if (!zip_.extract("META-INF/container.xml", xml, error, 65536)) return false;
  tinyxml2::XMLDocument container;
  if (container.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) return fail(error, "Invalid EPUB container XML");
  auto* roots = child(container.RootElement(), "rootfiles");
  auto* root = child(roots, "rootfile");
  if (!root || !root->Attribute("full-path")) return fail(error, "EPUB package path not found");
  std::string package = normalizedPath("", root->Attribute("full-path"));
  if (!zip_.extract(package, xml, error, 512 * 1024)) return false;
  if (std::count(xml.begin(), xml.end(), '<') + std::count(xml.begin(), xml.end(), '=') > 16000)
    return fail(error, "EPUB package has too much markup for available memory");
  tinyxml2::XMLDocument opf;
  if (opf.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) return fail(error, "Invalid EPUB package XML");
  auto* metadata = child(opf.RootElement(), "metadata");
  auto* name = child(metadata, "title");
  if (name && name->GetText()) title = name->GetText();
  auto* manifest = child(opf.RootElement(), "manifest");
  auto* order = child(opf.RootElement(), "spine");
  if (!manifest || !order) return fail(error, "EPUB reading order missing");
  const auto slash = package.find_last_of('/');
  std::string base = slash == std::string::npos ? "" : package.substr(0, slash + 1);
  std::map<std::string, std::string> items;
  std::string navPath, ncxPath;
  const char* ncxId = order->Attribute("toc");
  for (auto* item = manifest->FirstChildElement(); item; item = item->NextSiblingElement()) {
    const char* id = item->Attribute("id"), *href = item->Attribute("href"), *type = item->Attribute("media-type");
    if (id && href && type && (!std::strcmp(type, "application/xhtml+xml") || !std::strcmp(type, "text/html")))
      items[id] = normalizedPath(base, href);
    if (href && token(item->Attribute("properties"), "nav")) navPath = normalizedPath(base, href);
    if (id && href && type && !std::strcmp(type, "application/x-dtbncx+xml") &&
        (!ncxId || !std::strcmp(ncxId, id))) ncxPath = normalizedPath(base, href);
  }
  for (auto* item = order->FirstChildElement(); item; item = item->NextSiblingElement()) {
    if (item->Attribute("linear", "no")) continue;
    const char* id = item->Attribute("idref");
    if (!id || items.find(id) == items.end()) return fail(error, "Unsupported non-text EPUB spine item");
    spine.push_back(items[id]);
    if (spine.size() > 4096) return fail(error, "EPUB has too many chapters");
  }
  if (spine.empty()) return fail(error, "EPUB has no readable chapters");
  container.Clear(); opf.Clear(); xml.clear(); xml.shrink_to_fit();
  if (!navPath.empty()) {
    readNavigation(navPath, false, false);
    readNavigation(navPath, false, true);
  }
  if (printedPages.empty() && !ncxPath.empty()) readNavigation(ncxPath, true, false);
  if (chapters.empty() && !ncxPath.empty()) readNavigation(ncxPath, true, true);
  hasToc = !chapters.empty();
  if (!hasToc) for (size_t i = 0; i < spine.size(); ++i)
    chapters.push_back({"Section " + std::to_string(i + 1), i, ""});
  return true;
}

static void nodeText(tinyxml2::XMLNode* node, std::string& text) {
  if (!node) return;
  if (auto* t = node->ToText()) text += t->Value();
  for (auto* c = node->FirstChild(); c; c = c->NextSibling()) nodeText(c, text);
}
void Epub::readNavigation(const std::string& path, bool ncx, bool toc) {
  auto& targets = toc ? chapters : printedPages;
  std::string xml, error;
  if (!zip_.extract(path, xml, error, 256 * 1024) ||
      std::count(xml.begin(), xml.end(), '<') + std::count(xml.begin(), xml.end(), '=') > 16000) return;
  tinyxml2::XMLDocument doc;
  if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) return;
  tinyxml2::XMLElement* list = nullptr;
  std::function<void(tinyxml2::XMLElement*)> findList = [&](tinyxml2::XMLElement* node) {
    if (!node || list) return;
    std::string tag = localName(node->Name());
    if ((ncx && tag == (toc ? "navMap" : "pageList")) || (!ncx && tag == "nav" &&
        (token(attribute(node, "type"), toc ? "toc" : "page-list") ||
         token(node->Attribute("role"), toc ? "doc-toc" : "doc-pagelist")))) {
      list = node; return;
    }
    for (auto* c = node->FirstChildElement(); c; c = c->NextSiblingElement()) findList(c);
  };
  findList(doc.RootElement());
  if (!list) return;
  auto slash = path.find_last_of('/');
  std::string base = slash == std::string::npos ? "" : path.substr(0, slash + 1);
  std::function<void(tinyxml2::XMLElement*)> links = [&](tinyxml2::XMLElement* node) {
    if (!node || targets.size() >= 4096) return;
    std::string tag = localName(node->Name()), label;
    const char* href = nullptr;
    if (!ncx && tag == "a") { href = node->Attribute("href"); nodeText(node, label); }
    if (ncx && tag == (toc ? "navPoint" : "pageTarget")) {
      auto* content = child(node, "content");
      if (content) href = content->Attribute("src");
      nodeText(child(node, "navLabel"), label);
      if (label.empty() && node->Attribute("value")) label = node->Attribute("value");
    }
    if (href && std::strchr(href, ':') == nullptr) {
      size_t first = label.find_first_not_of(" \t\r\n"), last = label.find_last_not_of(" \t\r\n");
      label = first == std::string::npos ? "" : label.substr(first, last - first + 1);
      std::string target(href), fragment;
      auto hash = target.find('#');
      if (hash != std::string::npos) { fragment = decodeUri(target.substr(hash + 1)); target.resize(hash); }
      target = target.empty() ? path : normalizedPath(base, target);
      auto chapter = std::find(spine.begin(), spine.end(), target);
      size_t limit = toc ? 256 : 64;
      if (label.size() > limit) {
        while (limit > 0 && (uint8_t(label[limit]) & 0xc0) == 0x80) --limit;
        label.resize(limit);
      }
      if (!label.empty() && fragment.size() <= 256 && chapter != spine.end())
        targets.push_back({label, size_t(chapter - spine.begin()), fragment});
    }
    for (auto* c = node->FirstChildElement(); c; c = c->NextSiblingElement()) links(c);
  };
  links(list);
}

static bool targetOffset(const NavigationTarget& target, const Anchors& anchors, uint32_t& offset) {
  if (target.fragment.empty()) { offset = 0; return true; }
  auto match = anchors.find(target.fragment);
  if (match == anchors.end()) return false;
  offset = match->second;
  return true;
}
bool Epub::printedPageOffset(size_t page, const Anchors& anchors, uint32_t& offset) const {
  return page < printedPages.size() && targetOffset(printedPages[page], anchors, offset);
}
bool Epub::chapterOffset(size_t chapter, const Anchors& anchors, uint32_t& offset) const {
  return chapter < chapters.size() && targetOffset(chapters[chapter], anchors, offset);
}
std::vector<uint32_t> Epub::pageBreakOffsets(size_t chapter, const Anchors& anchors) const {
  std::vector<uint32_t> breaks;
  for (size_t i = 0; i < printedPages.size(); ++i) {
    uint32_t offset;
    if (printedPages[i].chapter == chapter && printedPageOffset(i, anchors, offset)) breaks.push_back(offset);
  }
  for (size_t i = 0; i < chapters.size(); ++i) {
    uint32_t offset;
    if (chapters[i].chapter == chapter && chapterOffset(i, anchors, offset)) breaks.push_back(offset);
  }
  return breaks;
}

static void appendCodepoint(std::string& output, uint32_t c) {
  if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) || c == 0) return;
  if (c < 0x80) output += char(c);
  else if (c < 0x800) { output += char(0xc0 | (c >> 6)); output += char(0x80 | (c & 63)); }
  else if (c < 0x10000) { output += char(0xe0 | (c >> 12)); output += char(0x80 | ((c >> 6) & 63)); output += char(0x80 | (c & 63)); }
  else { output += char(0xf0 | (c >> 18)); output += char(0x80 | ((c >> 12) & 63)); output += char(0x80 | ((c >> 6) & 63)); output += char(0x80 | (c & 63)); }
}
std::string decodeEntities(const std::string& text) {
  static const std::map<std::string, std::string> entities = {
    {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "},
    {"mdash", "—"}, {"ndash", "–"}, {"hellip", "…"}, {"lsquo", "‘"}, {"rsquo", "’"},
    {"ldquo", "“"}, {"rdquo", "”"}, {"copy", "©"}, {"shy", ""}
  };
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '&') {
      size_t end = text.find(';', i + 1);
      if (end != std::string::npos && end - i <= 12) {
        std::string entity = text.substr(i + 1, end - i - 1);
        auto match = entities.find(entity);
        if (match != entities.end()) { out += match->second; i = end; continue; }
        if (entity.size() > 1 && entity[0] == '#') {
          bool hex = entity[1] == 'x' || entity[1] == 'X';
          char* stop; const char* start = entity.c_str() + (hex ? 2 : 1);
          unsigned long c = std::strtoul(start, &stop, hex ? 16 : 10);
          if (stop != start && *stop == 0) { appendCodepoint(out, c); i = end; continue; }
        }
      }
    }
    out += text[i];
  }
  return out;
}
static void paragraph(std::string& out) {
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (!out.empty() && out.back() != '\n') out += '\n';
}
static void flushAnchors(Anchors* anchors, std::vector<std::string>& pending, size_t offset) {
  if (anchors) for (const auto& id : pending) anchors->emplace(id, uint32_t(offset));
  pending.clear();
}
static void extractText(tinyxml2::XMLNode* node, std::string& out, Anchors* anchors,
                        std::vector<std::string>& pending) {
  if (auto* t = node->ToText()) {
    // XML entities have already been decoded once by tinyxml2.
    // Remaining HTML entities are decoded before XML parsing instead.
    for (const char* p = t->Value(); *p; ++p) {
      unsigned char c = *p;
      if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
        if (!out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
      } else { flushAnchors(anchors, pending, out.size()); out += char(c); }
    }
    return;
  }
  auto* element = node->ToElement();
  if (!element) return;
  std::string tag = localName(element->Name());
  if (tag == "head" || tag == "script" || tag == "style" || tag == "svg") return;
  if (anchors) {
    const char* id = attribute(element, "id");
    if (!id && tag == "a") id = element->Attribute("name");
    if (id && std::strlen(id) <= 256) pending.push_back(id);
  }
  bool block = tag == "p" || tag == "div" || tag == "br" || tag == "li" || tag == "section" ||
               tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4" || tag == "tr" || tag == "blockquote";
  if (block) paragraph(out);
  if (tag == "img") {
    if (const char* alt = element->Attribute("alt")) {
      if (!out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
      flushAnchors(anchors, pending, out.size());
      out += '['; out += alt; out += "] ";
    }
  }
  for (auto* c = node->FirstChild(); c; c = c->NextSibling()) extractText(c, out, anchors, pending);
  if (block) paragraph(out);
}

bool Epub::chapter(size_t index, std::string& text, std::string& error, Anchors* anchors) {
  text.clear();
  if (anchors) anchors->clear();
  if (index >= spine.size()) return fail(error, "Chapter out of range");
  std::string xml;
  if (!zip_.extract(spine[index], xml, error)) return false;
  // Bound XML node allocations as well as decompressed bytes. Pathological
  // markup can otherwise use many times the chapter size in tree nodes.
  if (std::count(xml.begin(), xml.end(), '<') + std::count(xml.begin(), xml.end(), '=') > 12000)
    return fail(error, "Chapter has too much markup for available memory");
  // Convert HTML named entities to numeric references, preserving XML syntax.
  std::string safe;
  safe.reserve(xml.size());
  for (size_t i = 0; i < xml.size(); ++i) {
    if (xml[i] == '&') {
      size_t end = xml.find(';', i + 1);
      if (end != std::string::npos && end - i <= 12) {
        std::string entity = xml.substr(i, end - i + 1);
        if (entity[1] != '#' && entity != "&amp;" && entity != "&lt;" && entity != "&gt;" && entity != "&quot;" && entity != "&apos;") {
          std::string decoded = decodeEntities(entity);
          if (decoded != entity) { safe += decoded; i = end; continue; }
        }
      }
    }
    safe += xml[i];
  }
  xml.clear(); xml.shrink_to_fit();
  tinyxml2::XMLDocument doc;
  if (doc.Parse(safe.data(), safe.size()) != tinyxml2::XML_SUCCESS) return fail(error, "Invalid XHTML chapter (well-formed EPUB required)");
  if (!doc.RootElement()) return fail(error, "Empty XHTML document");
  auto* body = child(doc.RootElement(), "body");
  std::vector<std::string> pending;
  extractText(body ? body : doc.RootElement(), text, anchors, pending);
  while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
  flushAnchors(anchors, pending, text.size());
  return true;
}

static size_t nextCharacter(const std::string& text, size_t pos) {
  ++pos;
  while (pos < text.size() && (uint8_t(text[pos]) & 0xc0) == 0x80) ++pos;
  return pos;
}
std::vector<Page> paginate(const std::string& text, int width, size_t linesPerPage, const Measure& measure,
                           const std::vector<uint32_t>& breaks) {
  std::vector<Page> pages;
  if (width <= 0 || !linesPerPage) return pages;
  std::vector<uint32_t> boundaries;
  for (uint32_t offset : breaks)
    if (offset > 0 && offset < text.size() && (uint8_t(text[offset]) & 0xc0) != 0x80) boundaries.push_back(offset);
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
  boundaries.push_back(text.size());
  size_t boundary = 0;
  Page page{0, {}};
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t segmentEnd = boundaries[boundary];
    size_t begin = pos, end = pos, lastSpace = std::string::npos;
    while (end < segmentEnd && text[end] != '\n') {
      size_t next = nextCharacter(text, end);
      if (measure(text.substr(begin, next - begin)) > width) break;
      if (text[end] == ' ') lastSpace = end;
      end = next;
    }
    size_t lineEnd = end;
    if (end == segmentEnd) pos = end;
    else if (text[end] == '\n') pos = end + 1;
    else if (lastSpace != std::string::npos) { lineEnd = lastSpace; pos = lastSpace + 1; }
    else { if (end == begin) end = nextCharacter(text, begin); lineEnd = end; pos = end; }
    while (pos < segmentEnd && text[pos] == ' ') ++pos;
    if (page.lines.empty()) page.offset = begin;
    page.lines.push_back({uint32_t(begin), uint32_t(lineEnd)});
    if (page.lines.size() == linesPerPage) { pages.push_back(std::move(page)); page = {uint32_t(pos), {}}; }
    if (pos == segmentEnd) {
      if (!page.lines.empty()) { pages.push_back(std::move(page)); page = {uint32_t(pos), {}}; }
      ++boundary;
    }
  }
  if (!page.lines.empty() || pages.empty()) pages.push_back(std::move(page));
  return pages;
}
size_t pageForOffset(const std::vector<Page>& pages, uint32_t offset) {
  size_t result = 0;
  for (size_t i = 0; i < pages.size() && pages[i].offset <= offset; ++i) result = i;
  return result;
}
uint32_t totalPages(const std::vector<uint32_t>& chapterCounts) {
  uint64_t total = 0;
  for (uint32_t count : chapterCounts) {
    if (!count) return 0;
    total += count;
  }
  return total <= UINT32_MAX ? uint32_t(total) : 0;
}
bool resolveBookPage(const std::vector<uint32_t>& chapterCounts, uint32_t number,
                     size_t& chapter, size_t& page) {
  uint32_t total = totalPages(chapterCounts);
  if (!number || number > total) return false;
  uint32_t offset = number - 1;
  for (size_t i = 0; i < chapterCounts.size(); ++i) {
    if (offset < chapterCounts[i]) { chapter = i; page = offset; return true; }
    offset -= chapterCounts[i];
  }
  return false;
}
} // namespace reader
