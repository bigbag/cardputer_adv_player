#include "wav_decoder.hpp"
#include <cstring>

static uint16_t readU16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

WavInfo wavParseHeader(const uint8_t* data, size_t len) {
  WavInfo info;

  if (len < 12) {
    info.error = "too short";
    return info;
  }
  if (std::memcmp(data, "RIFF", 4) != 0) {
    info.error = "not riff";
    return info;
  }
  if (std::memcmp(data + 8, "WAVE", 4) != 0) {
    info.error = "not wave";
    return info;
  }

  bool foundFmt = false;
  bool foundData = false;
  size_t pos = 12;

  while (pos + 8 <= len) {
    uint32_t chunkSize = readU32(data + pos + 4);
    if (std::memcmp(data + pos, "fmt ", 4) == 0) {
      if (pos + 8 + 16 > len) {
        info.error = "fmt truncated";
        return info;
      }
      const uint8_t* fmt = data + pos + 8;
      uint16_t formatTag = readU16(fmt);
      if (formatTag != 1) {
        info.error = "not pcm";
        return info;
      }
      info.channels = readU16(fmt + 2);
      info.sampleRate = readU32(fmt + 4);
      info.bitsPerSample = readU16(fmt + 14);

      if (info.channels < 1 || info.channels > 2) {
        info.error = "bad channels";
        return info;
      }
      if (info.bitsPerSample != 16) {
        info.error = "bad bps";
        return info;
      }
      foundFmt = true;
    } else if (std::memcmp(data + pos, "data", 4) == 0) {
      info.dataOffset = static_cast<uint32_t>(pos + 8);
      info.dataSize = chunkSize;
      foundData = true;
    }

    pos += 8 + chunkSize;
    if (chunkSize & 1) pos++;
  }

  if (!foundFmt) {
    info.error = "no fmt";
    return info;
  }
  if (!foundData) {
    info.error = "no data";
    return info;
  }

  info.valid = true;
  return info;
}

#ifndef UNIT_TEST
#include <SD.h>
#include <FS.h>

bool WavDecoder::open(const char* path) {
  close();
  fs::File f = SD.open(path, FILE_READ);
  if (!f) return false;

  uint8_t hdr[512];
  size_t n = f.read(hdr, sizeof(hdr));
  info_ = wavParseHeader(hdr, n);
  if (!info_.valid) {
    f.close();
    return false;
  }

  fmt_.sampleRate = info_.sampleRate;
  fmt_.channels = info_.channels;
  fmt_.bitsPerSample = info_.bitsPerSample;
  uint32_t bytesPerSec = info_.sampleRate * info_.channels * (info_.bitsPerSample / 8);
  fmt_.durationMs = bytesPerSec ? (info_.dataSize * 1000u / bytesPerSec) : 0;

  f.seek(info_.dataOffset);
  file_ = new fs::File(std::move(f));
  bytesRead_ = 0;
  return true;
}

void WavDecoder::close() {
  if (file_) {
    static_cast<fs::File*>(file_)->close();
    delete static_cast<fs::File*>(file_);
    file_ = nullptr;
  }
  bytesRead_ = 0;
  info_ = {};
  fmt_ = {};
}

AudioFormat WavDecoder::format() const {
  return fmt_;
}

DecodeStatus WavDecoder::decode(int16_t* outStereo, size_t maxFrames, size_t* gotFrames) {
  if (!file_ || !outStereo || !gotFrames) return DecodeStatus::Error;
  *gotFrames = 0;

  fs::File& f = *static_cast<fs::File*>(file_);
  uint32_t remaining = info_.dataSize - bytesRead_;
  if (remaining == 0) return DecodeStatus::Finished;

  size_t bytesPerFrame = info_.channels * 2;
  size_t framesToRead = maxFrames;
  if (framesToRead * bytesPerFrame > remaining) {
    framesToRead = remaining / bytesPerFrame;
  }
  if (framesToRead == 0) return DecodeStatus::Finished;

  if (info_.channels == 2) {
    size_t bytes = framesToRead * 4;
    size_t got = f.read(reinterpret_cast<uint8_t*>(outStereo), bytes);
    size_t frames = got / 4;
    *gotFrames = frames;
    bytesRead_ += static_cast<uint32_t>(got);
  } else {
    int16_t mono[256];
    size_t chunk = framesToRead < 256 ? framesToRead : 256;
    size_t totalFrames = 0;
    while (totalFrames < framesToRead) {
      size_t n = framesToRead - totalFrames;
      if (n > chunk) n = chunk;
      size_t got = f.read(reinterpret_cast<uint8_t*>(mono), n * 2);
      size_t frames = got / 2;
      for (size_t i = 0; i < frames; i++) {
        outStereo[(totalFrames + i) * 2] = mono[i];
        outStereo[(totalFrames + i) * 2 + 1] = mono[i];
      }
      totalFrames += frames;
      bytesRead_ += static_cast<uint32_t>(got);
      if (frames < n) break;
    }
    *gotFrames = totalFrames;
  }

  return *gotFrames > 0 ? DecodeStatus::Ok : DecodeStatus::Finished;
}

bool WavDecoder::seekMs(uint32_t ms) {
  if (!file_) return false;
  uint32_t bytesPerSec = info_.sampleRate * info_.channels * 2;
  uint32_t byteOffset = static_cast<uint32_t>((uint64_t)ms * bytesPerSec / 1000);
  if (byteOffset > info_.dataSize) byteOffset = info_.dataSize;
  uint32_t frameAlign = info_.channels * 2;
  byteOffset = (byteOffset / frameAlign) * frameAlign;

  fs::File& f = *static_cast<fs::File*>(file_);
  f.seek(info_.dataOffset + byteOffset);
  bytesRead_ = byteOffset;
  return true;
}

uint32_t WavDecoder::positionMs() const {
  uint32_t bytesPerSec = info_.sampleRate * info_.channels * 2;
  if (bytesPerSec == 0) return 0;
  return static_cast<uint32_t>((uint64_t)bytesRead_ * 1000 / bytesPerSec);
}

#else

bool WavDecoder::open(const char*) { return false; }
void WavDecoder::close() {}
AudioFormat WavDecoder::format() const { return fmt_; }
DecodeStatus WavDecoder::decode(int16_t*, size_t, size_t*) { return DecodeStatus::Error; }
bool WavDecoder::seekMs(uint32_t) { return false; }
uint32_t WavDecoder::positionMs() const { return 0; }

#endif
