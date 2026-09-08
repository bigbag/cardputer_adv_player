#pragma once

// Native stand-in for the Arduino FS.h header.
// It backs Settings save and load with in-memory files and adds failure
// injection hooks for the tests.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

static constexpr const char* FILE_READ = "r";
static constexpr const char* FILE_WRITE = "w";

// Silent stand-in for HardwareSerial. Settings logs through Serial.
struct NativeSerial {
  void begin(unsigned long) {}
  void println(const char* = "") {}
  void printf(const char*, ...) {}
  void flush() {}
};
inline NativeSerial Serial;

// Handle for one open file. Writing appends, reading consumes.
// shortWrites points at the owner SD card's injection counter.
class File {
 public:
  File() = default;
  explicit operator bool() const { return data_ != nullptr; }

  size_t write(const uint8_t* buf, size_t n) {
    if (!writable_ || data_ == nullptr || n == 0) return 0;
    size_t count = n;
    if (shortWrites_ != nullptr && *shortWrites_ > 0) {
      --*shortWrites_;
      count = n - 1;
    }
    data_->insert(data_->end(), buf, buf + count);
    return count;
  }

  int read() {
    if (data_ == nullptr || pos_ >= data_->size()) return -1;
    return (*data_)[pos_++];
  }

  int available() const {
    return data_ == nullptr ? 0 : static_cast<int>(data_->size() - pos_);
  }

  void flush() {}
  size_t size() const { return data_ == nullptr ? 0 : data_->size(); }
  void close() {
    data_ = nullptr;
    shortWrites_ = nullptr;
    writable_ = false;
  }

 private:
  friend class SDClass;
  File(std::vector<uint8_t>* data, int* shortWrites, bool writable)
      : data_(data), shortWrites_(shortWrites), writable_(writable) {}

  std::vector<uint8_t>* data_ = nullptr;
  int* shortWrites_ = nullptr;
  size_t pos_ = 0;
  bool writable_ = false;
};
