#pragma once

// Use real fixture bytes with injected read and seek failures.

#include "decoders/decoder_input.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

class Mp3TestFile : public DecoderInput {
 public:
  Mp3TestFile() = default;

  // Backed by caller bytes (copied).
  static Mp3TestFile fromBytes(const uint8_t* data, size_t size) {
    Mp3TestFile f;
    f.bytes_.assign(data, data + size);
    return f;
  }

  // Backed by a real fixture file loaded into memory.
  bool openFile(const char* path) {
    std::FILE* fp = std::fopen(path, "rb");
    if (!fp) return false;
    std::fseek(fp, 0, SEEK_END);
    long n = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (n < 0) { std::fclose(fp); return false; }
    bytes_.resize(static_cast<size_t>(n));
    size_t got = n ? std::fread(bytes_.data(), 1, bytes_.size(), fp) : 0;
    std::fclose(fp);
    if (got != bytes_.size()) { bytes_.clear(); return false; }
    pos_ = 0;
    return true;
  }

  // Truncate in memory to derive an EOF/partial-frame variant.
  void truncateTo(size_t size) {
    if (size < bytes_.size()) bytes_.resize(size);
    if (pos_ > bytes_.size()) pos_ = static_cast<uint32_t>(bytes_.size());
  }

  // Cap every read to this many bytes; 0 disables. Simulates slow SD chunks
  // and short reads across frame headers.
  void setReadLimit(uint32_t maxBytes) { readLimit_ = maxBytes; }

  // Make the next N reads return 0 before end of file (I/O failure).
  void failNextReads(uint32_t count) { failedReadsLeft_ = count; }

  // Make the next N seeks fail; the decoder must keep its previous state.
  void failNextSeeks(uint32_t count) { failedSeeksLeft_ = count; }

  size_t read(uint8_t* dst, size_t bytes) override {
    if (failedReadsLeft_ > 0 && pos_ < bytes_.size()) {
      failedReadsLeft_--;
      return 0;
    }
    if (pos_ >= bytes_.size()) return 0;
    size_t avail = bytes_.size() - pos_;
    size_t n = bytes < avail ? bytes : avail;
    if (readLimit_ != 0 && n > readLimit_) n = readLimit_;
    std::memcpy(dst, bytes_.data() + pos_, n);
    pos_ += static_cast<uint32_t>(n);
    totalRead_ += static_cast<uint64_t>(n);
    return n;
  }

  // Total bytes ever returned by read(); lets tests verify bounded read
  // work (e.g. the estimate scan must not sweep a long junk gap).
  uint64_t totalBytesRead() const { return totalRead_; }

  bool seek(uint32_t pos) override {
    if (failedSeeksLeft_ > 0) {
      failedSeeksLeft_--;
      return false;
    }
    if (pos > bytes_.size()) return false;
    pos_ = pos;
    return true;
  }

  uint32_t position() override { return pos_; }
  uint32_t size() override { return static_cast<uint32_t>(bytes_.size()); }
  bool valid() override { return true; }
  void close() override {}

  const uint8_t* data() const { return bytes_.data(); }
  size_t byteSize() const { return bytes_.size(); }

 private:
  std::vector<uint8_t> bytes_;
  uint32_t pos_ = 0;
  uint64_t totalRead_ = 0;
  uint32_t readLimit_ = 0;
  uint32_t failedReadsLeft_ = 0;
  uint32_t failedSeeksLeft_ = 0;
};
