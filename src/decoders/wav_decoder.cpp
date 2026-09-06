#include "wav_decoder.hpp"
#include "config.hpp"
#include <cstring>

namespace {

constexpr size_t kRiffHeader = 12;
constexpr size_t kChunkHeader = 8;
constexpr size_t kFmtBytes = 16;

uint16_t readU16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

bool readExact(WavReadAt readAt, void* context, uint32_t offset,
               uint8_t* destination, size_t size) {
  return readAt(context, offset, destination, size) == size;
}

}  // namespace

WavInfo wavParseStream(WavReadAt readAt, void* context, uint32_t fileSize) {
  WavInfo info;
  if (!readAt) {
    info.error = "no reader";
    return info;
  }

  uint8_t riff[kRiffHeader];
  if (!readExact(readAt, context, 0, riff, sizeof(riff))) {
    info.error = "short riff header";
    return info;
  }
  if (std::memcmp(riff, "RIFF", 4) != 0) {
    info.error = "not riff";
    return info;
  }
  if (std::memcmp(riff + 8, "WAVE", 4) != 0) {
    info.error = "not wave";
    return info;
  }

  // Declared RIFF end must fit inside the file; bytes past it are ignored.
  const uint64_t riffEnd = 8ull + readU32(riff + 4);
  if (riffEnd > fileSize) {
    info.error = "riff exceeds file";
    return info;
  }

  uint8_t fmt[kFmtBytes];
  bool haveFmt = false;
  bool haveData = false;
  uint64_t pos = kRiffHeader;

  while (pos + kChunkHeader <= riffEnd) {
    uint8_t chk[kChunkHeader];
    if (!readExact(readAt, context, static_cast<uint32_t>(pos), chk, sizeof(chk))) {
      info.error = "short chunk header";
      return info;
    }
    const uint32_t chunkSize = readU32(chk + 4);
    const uint64_t payload = pos + kChunkHeader;
    const uint64_t chunkEnd = payload + chunkSize;
    if (chunkEnd > riffEnd) {
      info.error = "chunk exceeds riff";
      return info;
    }
    if ((chunkSize & 1) && chunkEnd + 1 > riffEnd) {
      // The declared RIFF size counts the pad byte; a missing pad is a
      // truncated container, not a valid final chunk.
      info.error = "missing pad";
      return info;
    }

    if (!haveFmt && std::memcmp(chk, "fmt ", 4) == 0) {
      if (chunkSize < kFmtBytes) {
        info.error = "fmt too small";
        return info;
      }
      if (!readExact(readAt, context, static_cast<uint32_t>(payload), fmt, sizeof(fmt))) {
        info.error = "short fmt";
        return info;
      }
      const uint16_t tag = readU16(fmt);
      info.channels = readU16(fmt + 2);
      info.sampleRate = readU32(fmt + 4);
      const uint32_t byteRate = readU32(fmt + 8);
      const uint16_t blockAlign = readU16(fmt + 12);
      info.bitsPerSample = readU16(fmt + 14);
      if (tag != 1) {
        info.error = "not pcm";
        return info;
      }
      if (info.channels < 1 || info.channels > 2) {
        info.error = "bad channels";
        return info;
      }
      if (info.bitsPerSample != 16) {
        info.error = "bad bps";
        return info;
      }
      if (info.sampleRate == 0) {
        info.error = "bad rate";
        return info;
      }
      if (blockAlign != info.channels * 2) {
        info.error = "bad block align";
        return info;
      }
      if (static_cast<uint64_t>(byteRate) !=
          static_cast<uint64_t>(info.sampleRate) * blockAlign) {
        info.error = "bad byte rate";
        return info;
      }
      haveFmt = true;
    } else if (!haveData && std::memcmp(chk, "data", 4) == 0) {
      info.dataOffset = static_cast<uint32_t>(payload);
      info.dataSize = chunkSize;
      haveData = true;
    }

    if (haveFmt && haveData) break;
    pos = chunkEnd + (chunkSize & 1);
  }

  if (!haveFmt) {
    info.error = "no fmt";
    return info;
  }
  if (!haveData) {
    info.error = "no data";
    return info;
  }
  if (info.dataSize % (info.channels * 2) != 0) {
    info.error = "partial frame";
    return info;
  }

  info.valid = true;
  return info;
}

uint32_t wavDurationMs(const WavInfo& info) {
  if (!info.valid || info.sampleRate == 0 || info.channels == 0) return 0;
  const uint64_t bytesPerSec = uint64_t(info.sampleRate) * info.channels * 2;
  const uint64_t duration = uint64_t(info.dataSize) * 1000 / bytesPerSec;
  return duration > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(duration);
}

uint32_t wavSeekByteOffset(const WavInfo& info, uint32_t targetMs) {
  if (!info.valid || info.sampleRate == 0 || info.channels == 0) return 0;
  const uint64_t bytesPerSec = uint64_t(info.sampleRate) * info.channels * 2;
  const uint32_t frameBytes = info.channels * 2;
  uint64_t offset = uint64_t(targetMs) * bytesPerSec / 1000;
  if (offset > info.dataSize) offset = info.dataSize;
  offset -= offset % frameBytes;
  return static_cast<uint32_t>(offset);
}

#ifndef UNIT_TEST
#include <SD.h>
#include <FS.h>

namespace {

size_t fileReadAt(void* context, uint32_t offset, uint8_t* destination, size_t size) {
  auto* f = static_cast<fs::File*>(context);
  if (!f->seek(offset)) return 0;
  return f->read(destination, size);
}

}  // namespace

bool WavDecoder::open(const char* path) {
  close();
  fs::File f = SD.open(path, FILE_READ);
  if (!f) return false;

  info_ = wavParseStream(fileReadAt, &f, f.size());
  if (!info_.valid) {
    f.close();
    return false;
  }

  fmt_.sampleRate = info_.sampleRate;
  fmt_.channels = info_.channels;
  fmt_.sourceChannels = info_.channels;
  fmt_.bitsPerSample = info_.bitsPerSample;
  fmt_.durationMs = wavDurationMs(info_);

  if (!f.seek(info_.dataOffset)) {
    f.close();
    return false;
  }
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
  const size_t frameBytes = info_.channels * 2;
  const uint32_t remaining = info_.dataSize - bytesRead_;
  if (remaining == 0) return DecodeStatus::Finished;

  size_t framesToRead = maxFrames;
  if (framesToRead > remaining / frameBytes) framesToRead = remaining / frameBytes;
  if (framesToRead == 0) return DecodeStatus::Finished;

  if (info_.channels == 2) {
    const size_t bytes = framesToRead * 4;
    const size_t got = f.read(reinterpret_cast<uint8_t*>(outStereo), bytes);
    if (got < bytes) {
#if AUDIO_DIAG
      // Requested bytes never exceeded the data chunk, so a short read
      // here is a real read failure, not normal end of file.
      Serial.printf("[audio] wav short read at byte %u req=%u got=%u\n",
                    static_cast<unsigned>(info_.dataOffset + bytesRead_),
                    static_cast<unsigned>(bytes), static_cast<unsigned>(got));
#endif
      // Count only complete frames so channel alignment survives.
      const size_t frames = got / 4;
      *gotFrames = frames;
      bytesRead_ += static_cast<uint32_t>(frames * 4);
      return DecodeStatus::Error;
    }
    *gotFrames = framesToRead;
    bytesRead_ += static_cast<uint32_t>(bytes);
  } else {
    int16_t mono[256];
    size_t totalFrames = 0;
    while (totalFrames < framesToRead) {
      size_t n = framesToRead - totalFrames;
      if (n > 256) n = 256;
      const size_t got = f.read(reinterpret_cast<uint8_t*>(mono), n * 2);
      if (got < n * 2) {
#if AUDIO_DIAG
        Serial.printf("[audio] wav short read at byte %u req=%u got=%u\n",
                      static_cast<unsigned>(info_.dataOffset + bytesRead_),
                      static_cast<unsigned>(n * 2), static_cast<unsigned>(got));
#endif
        const size_t frames = got / 2;
        for (size_t i = 0; i < frames; i++) {
          outStereo[(totalFrames + i) * 2] = mono[i];
          outStereo[(totalFrames + i) * 2 + 1] = mono[i];
        }
        totalFrames += frames;
        bytesRead_ += static_cast<uint32_t>(frames * 2);
        break;
      }
      for (size_t i = 0; i < n; i++) {
        outStereo[(totalFrames + i) * 2] = mono[i];
        outStereo[(totalFrames + i) * 2 + 1] = mono[i];
      }
      totalFrames += n;
      bytesRead_ += static_cast<uint32_t>(n * 2);
    }
    *gotFrames = totalFrames;
    if (totalFrames < framesToRead) return DecodeStatus::Error;
  }

  return DecodeStatus::Ok;
}

bool WavDecoder::seekMs(uint32_t ms) {
  if (!file_) return false;
  const uint32_t byteOffset = wavSeekByteOffset(info_, ms);
  fs::File& f = *static_cast<fs::File*>(file_);
  if (!f.seek(info_.dataOffset + byteOffset)) return false;
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
