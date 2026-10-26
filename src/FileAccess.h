#pragma once
#include <string>

namespace fileaccess {
inline bool uploadFilename(const std::string& name) {
  if (name.size() <= 5 || name.size() > 96 || name[0] == '.') return false;
  std::string suffix = name.substr(name.size() - 5);
  for (char& c : suffix) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  if (suffix != ".epub") return false;
  for (unsigned char c : name) if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':') return false;
  return true;
}
// API paths are decoded once by WebServer. Never normalize traversal, hidden
// staging files, or control characters into an accessible filesystem path.
inline bool publicPath(const std::string& path, bool allowRoot = false) {
  if (path.empty() || path.size() > 1024 || path[0] != '/') return false;
  if (path == "/") return allowRoot;
  size_t start = 1;
  for (size_t i = 1; i <= path.size(); ++i) {
    if (i < path.size()) {
      unsigned char c = path[i];
      if (c < 32 || c == 127 || c == '\\') return false;
    }
    if (i == path.size() || path[i] == '/') {
      if (i == start || path[start] == '.') return false;
      start = i + 1;
    }
  }
  return true;
}
inline std::string jsonString(const std::string& value) {
  static const char* hex = "0123456789abcdef";
  std::string result = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
    else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
    else result += char(c);
  }
  return result + '"';
}
inline std::string encodedFilename(const std::string& value) {
  static const char* hex = "0123456789ABCDEF";
  std::string result;
  for (unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') result += char(c);
    else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
  }
  return result;
}
} // namespace fileaccess
