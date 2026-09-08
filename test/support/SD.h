#pragma once

// Native stand-in for the Arduino SD library.
// The files live in a map. Tests seed fixtures through the public members and
// inject failures through the fail* members.

#include "FS.h"

class SDClass {
 public:
  std::map<std::string, std::vector<uint8_t>> files;
  std::set<std::string> dirs;

  // Failure injection.
  int shortWrites = 0;                  // next N write() calls drop one byte
  std::set<std::string> failRenameSrc;  // rename() fails for these sources
  std::set<std::string> failRemove;
  bool failOpenWrite = false;           // open(..., FILE_WRITE) fails
  bool failMkdir = false;               // mkdir() fails

  void reset() {
    files.clear();
    dirs.clear();
    shortWrites = 0;
    failRenameSrc.clear();
    failRemove.clear();
    failOpenWrite = false;
    failMkdir = false;
  }

  bool exists(const char* path) const {
    return files.count(path) > 0 || dirs.count(path) > 0;
  }

  bool mkdir(const char* path) {
    if (failMkdir) return false;
    dirs.insert(path);
    return true;
  }

  bool remove(const char* path) {
    return failRemove.count(path) == 0 && files.erase(path) > 0;
  }

  bool rename(const char* from, const char* to) {
    if (failRenameSrc.count(from) > 0 || exists(to)) return false;
    auto it = files.find(from);
    if (it == files.end()) return false;
    auto value = std::move(it->second);
    files.erase(it);
    files[to] = std::move(value);
    return true;
  }

  File open(const char* path, const char* mode) {
    if (std::strcmp(mode, FILE_WRITE) == 0) {
      if (failOpenWrite) return File();
      files[path].clear();
      return File(&files[path], &shortWrites, true);  // Truncate or create.
    }
    auto it = files.find(path);
    if (it == files.end()) return File();
    return File(&it->second, &shortWrites, false);
  }
};

inline SDClass SD;
