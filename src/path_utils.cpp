#include "path_utils.hpp"
#include <cstdio>
#include <cstring>
#include <cctype>

namespace path {

static int tolower_ascii(int c) {
  return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

void toLowerAscii(char* s) {
  if (!s) return;
  for (; *s; ++s) *s = static_cast<char>(tolower_ascii(static_cast<unsigned char>(*s)));
}

bool join(char* dest, size_t destCap, const char* dir, const char* name) {
  if (!dest || destCap == 0 || !dir || !name) return false;
  if (std::strcmp(dir, "/") == 0) {
    return std::snprintf(dest, destCap, "/%s", name) < static_cast<int>(destCap);
  }
  return std::snprintf(dest, destCap, "%s/%s", dir, name) < static_cast<int>(destCap);
}

bool parent(char* dest, size_t destCap, const char* path) {
  if (!dest || destCap == 0 || !path) return false;
  if (path[0] == '\0' || std::strcmp(path, "/") == 0) {
    std::snprintf(dest, destCap, "/");
    return true;
  }
  std::snprintf(dest, destCap, "%s", path);
  size_t n = std::strlen(dest);
  while (n > 1 && dest[n - 1] == '/') {
    dest[--n] = '\0';
  }
  char* slash = std::strrchr(dest, '/');
  if (!slash || slash == dest) {
    std::snprintf(dest, destCap, "/");
    return true;
  }
  *slash = '\0';
  if (dest[0] == '\0') std::snprintf(dest, destCap, "/");
  return true;
}

void fileName(char* dest, size_t destCap, const char* path) {
  if (!dest || destCap == 0) return;
  if (!path) { dest[0] = '\0'; return; }
  const char* base = std::strrchr(path, '/');
  base = base ? base + 1 : path;
  std::snprintf(dest, destCap, "%s", base);
}

bool hasExtInsensitive(const char* name, const char* extWithDot) {
  if (!name || !extWithDot) return false;
  size_t nlen = std::strlen(name);
  size_t elen = std::strlen(extWithDot);
  if (nlen < elen) return false;
  const char* tail = name + (nlen - elen);
  for (size_t i = 0; i < elen; ++i) {
    if (tolower_ascii(static_cast<unsigned char>(tail[i])) !=
        tolower_ascii(static_cast<unsigned char>(extWithDot[i]))) {
      return false;
    }
  }
  return true;
}

EntryKind kindFromName(const char* name) {
  if (hasExtInsensitive(name, ".mp3")) return EntryKind::Mp3;
  if (hasExtInsensitive(name, ".wav")) return EntryKind::Wav;
  return EntryKind::Dir;
}

}  // namespace path
