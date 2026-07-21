#pragma once

#include "config.hpp"
#include <cstddef>
#include <cstring>

struct BrowserHistoryFrame {
  char path[cfg::kMaxPathLen]{};
  size_t cursor = 0;
  size_t scroll = 0;
};

class BrowserHistory {
 public:
  static constexpr size_t kCapacity = 16;

  bool push(const char* path, size_t cursor, size_t scroll) {
    if (!path || path[0] != '/') return false;
    if (size_ >= kCapacity) {
      clear();
      return false;
    }
    BrowserHistoryFrame& frame = frames_[size_++];
    std::strncpy(frame.path, path, sizeof(frame.path) - 1);
    frame.path[sizeof(frame.path) - 1] = '\0';
    frame.cursor = cursor;
    frame.scroll = scroll;
    return true;
  }

  bool peek(BrowserHistoryFrame* out) const {
    if (!out || size_ == 0) return false;
    *out = frames_[size_ - 1];
    return true;
  }

  void discardTop() {
    if (size_ > 0) --size_;
  }

  void clear() { size_ = 0; }
  size_t size() const { return size_; }

  static void clampPosition(size_t count, size_t visibleRows,
                            size_t* cursor, size_t* scroll) {
    if (!cursor || !scroll || count == 0 || visibleRows == 0) {
      if (cursor) *cursor = 0;
      if (scroll) *scroll = 0;
      return;
    }
    if (*cursor >= count) *cursor = count - 1;
    const size_t maxScroll = count > visibleRows ? count - visibleRows : 0;
    if (*scroll > maxScroll) *scroll = maxScroll;
    if (*cursor < *scroll) {
      *scroll = *cursor;
    } else if (*cursor >= *scroll + visibleRows) {
      *scroll = *cursor - visibleRows + 1;
    }
  }

 private:
  BrowserHistoryFrame frames_[kCapacity]{};
  size_t size_ = 0;
};
