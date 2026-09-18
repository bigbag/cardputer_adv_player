#pragma once

#include "config.hpp"
#include <cstddef>
#include <cstring>

struct RecentEntry {
  char path[cfg::kMaxPathLen]{};
  uint32_t positionMs = 0;
};

class RecentList {
 public:
  static constexpr size_t kCapacity = cfg::kRecentCount;

  size_t size() const { return size_; }

  const RecentEntry* entry(size_t i) const {
    return i < size_ ? &entries_[i] : nullptr;
  }

  bool touch(const char* absPath, uint32_t positionMs) {
    if (!absPath || absPath[0] != '/' || std::strlen(absPath) >= cfg::kMaxPathLen) {
      return false;
    }


    size_t existing = kCapacity;
    for (size_t i = 0; i < size_; ++i) {
      if (std::strcmp(entries_[i].path, absPath) == 0) {
        existing = i;
        break;
      }
    }

    if (existing == 0) {
      entries_[0].positionMs = positionMs;
      return true;
    }

    RecentEntry item{};
    std::strncpy(item.path, absPath, sizeof(item.path) - 1);
    item.path[sizeof(item.path) - 1] = '\0';
    item.positionMs = positionMs;

    if (existing < size_) {
      for (size_t i = existing; i + 1 < size_; ++i) {
        entries_[i] = entries_[i + 1];
      }
      --size_;
    } else if (size_ == kCapacity) {
      --size_;
    }

    for (size_t i = size_; i > 0; --i) {
      entries_[i] = entries_[i - 1];
    }
    entries_[0] = item;
    ++size_;
    return true;
  }

  bool removeAt(size_t i) {
    if (i >= size_) return false;
    for (size_t n = i; n + 1 < size_; ++n) {
      entries_[n] = entries_[n + 1];
    }
    entries_[--size_] = {};
    return true;
  }

  bool setPosition(size_t i, uint32_t positionMs) {
    if (i >= size_) return false;
    entries_[i].positionMs = positionMs;
    return true;
  }

  bool timesDiffer(const uint32_t* last, size_t lastCount) const {
    if (size_ != lastCount) return true;
    if (size_ == 0) return false;
    if (!last) return true;
    for (size_t i = 0; i < size_; ++i) {
      if (entries_[i].positionMs / 1000 != last[i] / 1000) return true;
    }
    return false;
  }




  void setSlot(size_t i, const char* absPath, uint32_t positionMs) {
    if (i >= kCapacity) return;
    if (absPath && absPath[0] == '/') {
      if (std::strlen(absPath) >= sizeof(entries_[i].path)) return;
      std::strncpy(entries_[i].path, absPath, sizeof(entries_[i].path) - 1);
      entries_[i].path[sizeof(entries_[i].path) - 1] = '\0';
    } else {
      entries_[i].path[0] = '\0';
    }
    entries_[i].positionMs = positionMs;
    if (i + 1 > size_) size_ = i + 1;
  }



  void compact() {
    size_t w = 0;
    for (size_t r = 0; r < size_; ++r) {
      if (entries_[r].path[0] != '/') continue;
      if (w != r) entries_[w] = entries_[r];
      ++w;
    }
    for (size_t i = w; i < size_; ++i) entries_[i] = {};
    size_ = w;
  }

  void clear() {
    for (size_t i = 0; i < size_; ++i) entries_[i] = {};
    size_ = 0;
  }

 private:
  RecentEntry entries_[kCapacity]{};
  size_t size_ = 0;
};
