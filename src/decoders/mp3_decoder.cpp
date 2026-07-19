#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

#include "mp3_decoder.hpp"
#include "config.hpp"
#include <cstring>

#ifndef UNIT_TEST
#include <SD.h>
#include <FS.h>

struct Mp3Decoder::Impl {
  mp3dec_t dec{};
  fs::File file;
  AudioFormat fmt{};
  uint8_t inBuf[cfg::kCompressedBuf]{};
  size_t inBufUsed = 0;
  uint32_t fileSize = 0;
  uint32_t dataStart = 0;
  uint32_t samplesDecoded = 0;
  mp3d_sample_t pcmLeftover[MINIMP3_MAX_SAMPLES_PER_FRAME]{};
  size_t leftoverFrames = 0;
  size_t leftoverOffset = 0;
  uint16_t srcChannels = 0;
};

static size_t skipId3(fs::File& f) {
  uint8_t hdr[10];
  if (f.read(hdr, 10) != 10) { f.seek(0); return 0; }
  if (hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3') {
    uint32_t sz = ((uint32_t)(hdr[6] & 0x7F) << 21) |
                  ((uint32_t)(hdr[7] & 0x7F) << 14) |
                  ((uint32_t)(hdr[8] & 0x7F) << 7) |
                  (uint32_t)(hdr[9] & 0x7F);
    sz += 10;
    f.seek(sz);
    return sz;
  }
  f.seek(0);
  return 0;
}

bool Mp3Decoder::open(const char* path) {
  close();
  fs::File f = SD.open(path, FILE_READ);
  if (!f) return false;

  auto* p = new Impl();
  p->fileSize = f.size();
  p->file = std::move(f);

  p->dataStart = skipId3(p->file);
  mp3dec_init(&p->dec);

  size_t rd = p->file.read(p->inBuf, cfg::kCompressedBuf);
  p->inBufUsed = rd;

  mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  mp3dec_frame_info_t info{};
  int samples = mp3dec_decode_frame(&p->dec, p->inBuf, static_cast<int>(p->inBufUsed), pcm, &info);

  if (info.frame_bytes == 0 || info.hz == 0) {
    p->file.close();
    delete p;
    return false;
  }

  p->fmt.sampleRate = static_cast<uint32_t>(info.hz);
  p->fmt.channels = 2;
  p->fmt.bitsPerSample = 16;
  p->srcChannels = static_cast<uint16_t>(info.channels);

  uint32_t audioBytes = p->fileSize - p->dataStart;
  uint32_t avgBitrate = static_cast<uint32_t>(info.bitrate_kbps) * 1000u;
  if (avgBitrate > 0) {
    p->fmt.durationMs = static_cast<uint32_t>((uint64_t)audioBytes * 8000u / avgBitrate);
  }

  size_t consumed = static_cast<size_t>(info.frame_bytes);
  p->inBufUsed -= consumed;
  std::memmove(p->inBuf, p->inBuf + consumed, p->inBufUsed);

  if (samples > 0) {
    size_t frames = static_cast<size_t>(samples);
    if (p->srcChannels == 1) {
      for (size_t i = 0; i < frames; i++) {
        p->pcmLeftover[i * 2] = pcm[i];
        p->pcmLeftover[i * 2 + 1] = pcm[i];
      }
    } else {
      std::memcpy(p->pcmLeftover, pcm, frames * 2 * sizeof(mp3d_sample_t));
    }
    p->leftoverFrames = frames;
    p->leftoverOffset = 0;
    p->samplesDecoded = static_cast<uint32_t>(frames);
  }

  impl_ = p;
  return true;
}

void Mp3Decoder::close() {
  if (impl_) {
    impl_->file.close();
    delete impl_;
    impl_ = nullptr;
  }
}

AudioFormat Mp3Decoder::format() const {
  if (!impl_) return {};
  return impl_->fmt;
}

DecodeStatus Mp3Decoder::decode(int16_t* outStereo, size_t maxFrames, size_t* gotFrames) {
  if (!impl_ || !outStereo || !gotFrames) return DecodeStatus::Error;
  *gotFrames = 0;
  Impl& d = *impl_;
  size_t written = 0;

  while (written < maxFrames) {
    if (d.leftoverFrames > 0) {
      size_t avail = d.leftoverFrames;
      size_t need = maxFrames - written;
      size_t take = avail < need ? avail : need;
      std::memcpy(outStereo + written * 2, d.pcmLeftover + d.leftoverOffset * 2, take * 2 * sizeof(int16_t));
      d.leftoverOffset += take;
      d.leftoverFrames -= take;
      written += take;
      continue;
    }

    if (d.inBufUsed < cfg::kCompressedBuf) {
      size_t toRead = cfg::kCompressedBuf - d.inBufUsed;
      size_t got = d.file.read(d.inBuf + d.inBufUsed, toRead);
      d.inBufUsed += got;
    }

    if (d.inBufUsed == 0) break;

    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_frame_info_t info{};
    int samples = mp3dec_decode_frame(&d.dec, d.inBuf, static_cast<int>(d.inBufUsed), pcm, &info);

    if (info.frame_bytes == 0) break;

    size_t consumed = static_cast<size_t>(info.frame_bytes);
    d.inBufUsed -= consumed;
    std::memmove(d.inBuf, d.inBuf + consumed, d.inBufUsed);

    if (samples > 0) {
      size_t frames = static_cast<size_t>(samples);
      if (d.srcChannels == 1) {
        for (size_t i = 0; i < frames; i++) {
          d.pcmLeftover[i * 2] = pcm[i];
          d.pcmLeftover[i * 2 + 1] = pcm[i];
        }
      } else {
        std::memcpy(d.pcmLeftover, pcm, frames * 2 * sizeof(mp3d_sample_t));
      }
      d.leftoverFrames = frames;
      d.leftoverOffset = 0;
      d.samplesDecoded += static_cast<uint32_t>(frames);
    }
  }

  *gotFrames = written;
  if (written > 0) return DecodeStatus::Ok;
  return DecodeStatus::Finished;
}

bool Mp3Decoder::seekMs(uint32_t ms) {
  if (!impl_) return false;
  Impl& d = *impl_;

  if (d.fmt.durationMs == 0) return false;
  uint64_t audioBytes = d.fileSize - d.dataStart;
  uint64_t byteOffset = audioBytes * ms / d.fmt.durationMs;
  uint32_t seekPos = d.dataStart + static_cast<uint32_t>(byteOffset);
  if (seekPos >= d.fileSize) seekPos = d.fileSize > 512 ? d.fileSize - 512 : d.dataStart;

  d.file.seek(seekPos);
  d.inBufUsed = 0;
  d.leftoverFrames = 0;
  d.leftoverOffset = 0;
  mp3dec_init(&d.dec);

  d.samplesDecoded = static_cast<uint32_t>((uint64_t)ms * d.fmt.sampleRate / 1000);
  return true;
}

uint32_t Mp3Decoder::positionMs() const {
  if (!impl_ || impl_->fmt.sampleRate == 0) return 0;
  return static_cast<uint32_t>((uint64_t)impl_->samplesDecoded * 1000 / impl_->fmt.sampleRate);
}

#else

bool Mp3Decoder::open(const char*) { return false; }
void Mp3Decoder::close() {}
AudioFormat Mp3Decoder::format() const { return {}; }
DecodeStatus Mp3Decoder::decode(int16_t*, size_t, size_t*) { return DecodeStatus::Error; }
bool Mp3Decoder::seekMs(uint32_t) { return false; }
uint32_t Mp3Decoder::positionMs() const { return 0; }

#endif
