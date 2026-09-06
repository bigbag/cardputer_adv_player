#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

#include "mp3_decoder.hpp"
#include "mp3_info.hpp"
#include "config.hpp"
#include <cstring>
#include <new>

#if AUDIO_DIAG
#include <Arduino.h>
// Boundary logging for controlled error exits; compiled out of normal and
// native builds, never fired from the successful frame loop.
#define MP3_DIAG(...) Serial.printf(__VA_ARGS__)
#else
#define MP3_DIAG(...)
#endif

#ifdef UNIT_TEST
#include "mp3_test_file.hpp"
#else
#include <SD.h>
#endif

struct Mp3Decoder::Impl {
  mp3dec_t dec{};
  DecoderInput* in = nullptr;
  bool ownsInput = false;
  AudioFormat fmt{};
  uint8_t inBuf[cfg::kCompressedBuf]{};
  size_t inBufUsed = 0;
  uint32_t fileSize = 0;
  uint32_t logicalEnd = 0;  // audio ends here (ID3v1 trailer excluded)
  uint64_t audioStart = 0;  // first MPEG audio byte (after ID3 and any skip)
  bool inputEof = false;
  bool ioFailed = false;
  bool formatValid = false;
  bool tailGarbage = false;  // consumed tail bytes were not valid frames
  bool tailAllZero = true;   // every consumed tail byte was zero padding
  Mp3StreamInfo info{};
  bool prerollActive = false;
  uint32_t prerollTarget = 0;  // absolute byte of the seek target
  uint32_t warmupSynth = 0;    // PCM-producing frames since the seek
  uint16_t srcChannels = 0;
  // Decode target and delivery buffer in one: minimp3 writes the frame here,
  // mono frames expand backward in place to stereo.
  mp3d_sample_t pcmLeftover[MINIMP3_MAX_SAMPLES_PER_FRAME]{};
  size_t leftoverFrames = 0;
  size_t leftoverOffset = 0;
  uint64_t samplesDelivered = 0;  // stereo frames copied to the caller
};


// Fill the input window until full, confirmed end of the audio, or I/O
// failure. Reads never cross the logical end (a trailing ID3v1 tag is not
// stream audio). A positive short read is not end; a zero read before the
// known end is an I/O failure, a zero read at the known end is end.
void Mp3Decoder::refillWindow(Impl& d) {
  while (d.inBufUsed < cfg::kCompressedBuf && !d.inputEof && !d.ioFailed) {
    if (d.in->position() >= d.logicalEnd) {
      d.inputEof = true;
      return;
    }
    size_t want = cfg::kCompressedBuf - d.inBufUsed;
    if (d.logicalEnd - d.in->position() < want) {
      want = static_cast<size_t>(d.logicalEnd - d.in->position());
    }
    size_t got = d.in->read(d.inBuf + d.inBufUsed, want);
    if (got == 0) {
      if (d.in->position() >= d.logicalEnd) {
        d.inputEof = true;
      } else {
        d.ioFailed = true;
      }
      return;
    }
    d.inBufUsed += got;
  }
}

// One decode step over the current window. Consumes one frame or a garbage
// prefix, stages PCM for delivery unless stagePcm is false, and captures or
// validates the stream format. Returns false when the stream cannot
// progress; failStatus then carries the terminal status. On true,
// consumed > 0.
bool Mp3Decoder::frameStep(Impl& d, size_t& consumed, DecodeStatus& failStatus,
                           bool stagePcm, int* samplesOut) {
  consumed = 0;
  if (samplesOut) *samplesOut = 0;
  refillWindow(d);

  if (d.inBufUsed == 0) {
    if (d.ioFailed) {
      MP3_DIAG("[audio] mp3 read failure at byte %u\n",
               static_cast<unsigned>(d.in->position() - d.inBufUsed));
      failStatus = DecodeStatus::Error;
    } else {
      // A short non-sync tail (ID3v1 caught earlier, encoder padding,
      // LAME info strings) finishes cleanly; a large junk tail is a
      // controlled malformed-stream error.
      bool benign =
          d.formatValid && (!d.tailGarbage || d.tailAllZero);
      failStatus = benign ? DecodeStatus::Finished : DecodeStatus::Error;
      if (!benign) {
        MP3_DIAG("[audio] mp3 junk tail at byte %u\n",
                 static_cast<unsigned>(d.in->position()));
      }
    }
    return false;
  }

  // At the end of audio, limit input to the first complete Layer III frame
  // at the window start.
  // Apply the limit before format confirmation to support short padded files.
  // minimp3 can reject a final frame when padding follows it.
  // The limit selects its single-frame path without changing the bit reservoir.
  // Later steps classify the remaining tail bytes.
  int decodeBytes = static_cast<int>(d.inBufUsed);
  if (d.inputEof && decodeBytes > 4 && hdr_valid(d.inBuf) &&
      HDR_GET_LAYER(d.inBuf) == 1) {
    int frameLen =
        hdr_frame_bytes(d.inBuf, d.dec.free_format_bytes) + hdr_padding(d.inBuf);
    if (frameLen > 4 && frameLen <= decodeBytes) {
      decodeBytes = frameLen;
    }
  }

  bool windowWasFull = d.inBufUsed == cfg::kCompressedBuf;

  mp3dec_frame_info_t info{};
  int samples = mp3dec_decode_frame(&d.dec, d.inBuf, decodeBytes,
                                    d.pcmLeftover, &info);

  if (info.frame_bytes > 0) {
    // Bounded-span check before any offset arithmetic: minimp3 must never
    // report bytes outside the window it was given.
    if (info.frame_bytes > static_cast<int>(d.inBufUsed) || info.frame_offset < 0 ||
        info.frame_offset > info.frame_bytes) {
      MP3_DIAG("[audio] mp3 frame span %d/%d over window %u at byte %u\n",
               info.frame_bytes, info.frame_offset,
               static_cast<unsigned>(d.inBufUsed),
               static_cast<unsigned>(d.in->position() - d.inBufUsed));
      failStatus = DecodeStatus::Error;
      return false;
    }

    if (info.hz > 0 && info.channels > 0) {
      if (!d.formatValid) {
        if (info.layer != 3 || info.channels > 2) {
          // Not a supported Layer III stream.
          MP3_DIAG("[audio] mp3 unsupported layer %d at byte %u\n",
                   info.layer,
                   static_cast<unsigned>(d.in->position() - d.inBufUsed));
          failStatus = DecodeStatus::Error;
          return false;
        }
        d.fmt.sampleRate = static_cast<uint32_t>(info.hz);
        d.fmt.channels = 2;
        d.srcChannels = static_cast<uint16_t>(info.channels);
        d.fmt.sourceChannels = d.srcChannels;
        d.fmt.bitsPerSample = 16;
        d.formatValid = true;
        // The Xing/Info byte base is the first MPEG frame, which may sit
        // after skipped bytes; record its absolute offset. The tag is parsed
        // here on the bounded window before the input moves.
        size_t frameOff = static_cast<size_t>(info.frame_offset);
        size_t frameLen = static_cast<size_t>(info.frame_bytes) - frameOff;
        d.audioStart = d.in->position() - d.inBufUsed + frameOff;
        d.info = mp3ParseInfo(d.inBuf + frameOff, frameLen);
      } else if (info.hz != static_cast<int>(d.fmt.sampleRate) ||
                 info.channels != static_cast<int>(d.srcChannels) ||
                 info.layer != 3) {
        // Reject format changes within one file instead of converting with
        // stale channel information.
        MP3_DIAG("[audio] mp3 format change at byte %u: hz=%d ch=%d\n",
                 static_cast<unsigned>(d.in->position() - d.inBufUsed),
                 info.hz, info.channels);
        failStatus = DecodeStatus::Error;
        return false;
      }

      if (samples > 0) {
        if (stagePcm) {
          size_t frames = static_cast<size_t>(samples);
          if (d.srcChannels == 1) {
            // Backward in-place mono-to-stereo expansion.
            for (size_t i = frames; i-- > 0;) {
              d.pcmLeftover[i * 2] = d.pcmLeftover[i];
              d.pcmLeftover[i * 2 + 1] = d.pcmLeftover[i];
            }
          }
          d.leftoverFrames = frames;
          d.leftoverOffset = 0;
        }
        if (samplesOut) *samplesOut = samples;
      }
      d.tailGarbage = false;
      d.tailAllZero = true;
    } else {
      // A failed sync that swallowed the whole window without valid frame
      // info is an unsynchronized stream, same as the frame_bytes==0 case
      // below; sub-window garbage prefixes that reach valid frames and
      // short zero padding at the end stay allowed.
      if (!d.inputEof && windowWasFull) {
        MP3_DIAG("[audio] mp3 unsynchronized window at byte %u\n",
                 static_cast<unsigned>(d.in->position() - d.inBufUsed));
        failStatus = DecodeStatus::Error;
        return false;
      }
      // Consumed bytes no frame could claim. A sync-start span shorter
      // than its own header-declared frame is a truncated final frame
      // (clean finish); anything else is trailing padding (all zero, clean
      // finish) or junk (controlled error).
      bool syncStart = d.inBuf[0] == 0xFF && (d.inBuf[1] & 0xE0) == 0xE0;
      int frameLen = 0;
      if (syncStart && d.formatValid) {
        frameLen = hdr_frame_bytes(d.inBuf, d.dec.free_format_bytes) +
                   hdr_padding(d.inBuf);
      }
      if (syncStart && frameLen > static_cast<int>(d.inBufUsed)) {
        // Truncated final frame: keep the clean finish.
      } else {
        d.tailGarbage = true;
        // info.frame_bytes is the validated span, still pre-memmove here
        // (consumed is assigned after this branch).
        for (size_t i = 0; i < static_cast<size_t>(info.frame_bytes); i++) {
          if (d.inBuf[i] != 0) {
            d.tailAllZero = false;
            break;
          }
        }
      }
    }

    consumed = static_cast<size_t>(info.frame_bytes);
    d.inBufUsed -= consumed;
    std::memmove(d.inBuf, d.inBuf + consumed, d.inBufUsed);
    return true;
  }

  // No frame could be parsed from the window.
  if (!d.inputEof && d.inBufUsed == cfg::kCompressedBuf) {
    MP3_DIAG("[audio] mp3 unsynchronized window at byte %u\n",
             static_cast<unsigned>(d.in->position() - d.inBufUsed));
    failStatus = DecodeStatus::Error;  // full unsynchronized window
    return false;
  }
  if (d.ioFailed) {
    MP3_DIAG("[audio] mp3 read failure at byte %u\n",
             static_cast<unsigned>(d.in->position() - d.inBufUsed));
    failStatus = DecodeStatus::Error;
    return false;
  }
  bool benign = d.formatValid && (!d.tailGarbage || d.tailAllZero);
  if (!benign) {
    MP3_DIAG("[audio] mp3 junk tail at byte %u\n",
             static_cast<unsigned>(d.in->position() - d.inBufUsed));
  }
  failStatus = benign ? DecodeStatus::Finished : DecodeStatus::Error;
  return false;
}

// Limit each call to one input window of consumed bytes.
// This bound keeps corrupt streams from blocking the caller.
DecodeStatus Mp3Decoder::pump(Impl& d, int16_t* outStereo, size_t maxFrames,
                              size_t* gotFrames) {
  size_t written = 0;
  size_t workBytes = 0;
  DecodeStatus st = DecodeStatus::Ok;

  for (;;) {
    if (d.leftoverFrames > 0 && written < maxFrames) {
      size_t need = maxFrames - written;
      size_t take = d.leftoverFrames < need ? d.leftoverFrames : need;
      std::memcpy(outStereo + written * 2, d.pcmLeftover + d.leftoverOffset * 2,
                  take * 2 * sizeof(int16_t));
      d.leftoverOffset += take;
      d.leftoverFrames -= take;
      written += take;
      d.samplesDelivered += take;
      continue;
    }
    if (written >= maxFrames) break;

    if (workBytes >= cfg::kCompressedBuf) {
      // Input can still progress; let the caller observe stop and re-poll.
      st = DecodeStatus::NeedMore;
      break;
    }

    if (d.prerollActive) {
      // Approximate seek pre-roll: decode without delivering until the
      // first frame at or after the target byte, after two PCM-producing
      // warm-up frames. Frames without PCM still feed the bit reservoir.
      uint32_t frameStart = d.in->position() - d.inBufUsed;
      bool deliverable = frameStart >= d.prerollTarget && d.warmupSynth >= 2;
      int samples = 0;
      size_t consumed = 0;
      if (!frameStep(d, consumed, st, deliverable, &samples)) break;
      if (samples > 0) d.warmupSynth++;
      if (deliverable) d.prerollActive = false;
      workBytes += consumed;
      continue;
    }

    size_t consumed = 0;
    if (!frameStep(d, consumed, st, true, nullptr)) break;
    workBytes += consumed;
  }

  *gotFrames = written;
  return written > 0 ? DecodeStatus::Ok : st;
}

bool Mp3Decoder::openInput(DecoderInput& in) {
  close();
  if (!in.valid()) return false;

  auto* p = new (std::nothrow) Impl();
  if (!p) return false;
  impl_ = p;
  Impl& d = *p;
  d.in = &in;
  d.fileSize = in.size();
  d.logicalEnd = d.fileSize;

  // Tolerate positive short reads at the platform boundary.
  uint8_t head[10]{};
  size_t headGot = 0;
  while (headGot < sizeof(head)) {
    size_t got = in.read(head + headGot, sizeof(head) - headGot);
    if (got == 0) break;
    headGot += got;
  }
  if (headGot < sizeof(head)) { close(); return false; }
  uint32_t dataStart = 0;
  if (!mp3Id3DataStart(head, sizeof(head), d.fileSize, dataStart)) {
    MP3_DIAG("[audio] mp3 bad ID3v2 header on %u bytes\n",
             static_cast<unsigned>(d.fileSize));
    close();
    return false;
  }
  if (!in.seek(dataStart)) {
    MP3_DIAG("[audio] mp3 seek to %u failed\n", dataStart);
    close();
    return false;
  }
  d.audioStart = dataStart;

  // A trailing ID3v1 tag ('TAG' + 125 bytes) is metadata, not stream audio:
  // shrink the logical end so refills, EOF, and seek maps exclude it. Every
  // seek here is checked; a failed one fails the open.
  if (d.fileSize >= 128) {
    uint8_t tag[3];
    if (in.seek(static_cast<uint32_t>(d.fileSize - 128))) {
      // Tolerate positive short reads; the 3-byte TAG prefix decides.
      size_t got = 0;
      while (got < sizeof(tag)) {
        size_t n = in.read(tag + got, sizeof(tag) - got);
        if (n == 0) break;
        got += n;
      }
      if (got < sizeof(tag)) {
        MP3_DIAG("[audio] mp3 trailer read short at byte %u\n",
                 static_cast<unsigned>(d.fileSize - 128));
        close();
        return false;  // I/O failure before the audio end
      }
      if (tag[0] == 'T' && tag[1] == 'A' && tag[2] == 'G') {
        d.logicalEnd = d.fileSize - 128;
      }
    } else {
      MP3_DIAG("[audio] mp3 trailer seek to %u failed\n",
               static_cast<unsigned>(d.fileSize - 128));
      close();
      return false;  // I/O failure before the audio end
    }
    if (!in.seek(static_cast<uint32_t>(d.audioStart))) {
      MP3_DIAG("[audio] mp3 reseek to %u failed\n",
               static_cast<unsigned>(d.audioStart));
      close();
      return false;
    }
  }
  mp3dec_init(&d.dec);

  // Shared loop: open succeeds only at a valid frame; garbage and skipped
  // bytes may be consumed on the way.
  for (;;) {
    DecodeStatus st = DecodeStatus::Ok;
    size_t consumed = 0;
    if (frameStep(d, consumed, st, true, nullptr)) {
      if (d.formatValid) break;
      continue;  // bytes consumed but no usable frame yet
    }
    close();
    return false;  // end of file or malformed stream before any valid frame
  }

  // Trust the frame count only when the stream could plausibly hold it
  // (any Layer III frame is at least ~24 bytes) - checked whenever a frame
  // count is present, with or without the bytes field. A byte range or TOC
  // reaching outside the audio is discarded, and bad optional metadata
  // falls back to estimation instead of blocking playback.
  uint64_t availBytes = d.logicalEnd - d.audioStart;
  if (d.info.audioBytes > availBytes) {
    d.info.hasSeekToc = false;
    d.info.audioBytes = 0;
  }
  bool framesUsable = d.info.totalFrames > 0 &&
                      d.info.totalFrames <= availBytes / 24 &&
                      (d.info.audioBytes == 0 ||
                       d.info.audioBytes <= availBytes);
  if (framesUsable) {
    d.fmt.durationMs = d.info.durationMs;
    d.fmt.durationEstimated = false;
  } else {
    estimateDuration(d);
    d.fmt.durationEstimated = true;
    if (!restoreToStart(d)) { close(); return false; }
  }
  return true;
}

// Bounded fallback estimate: scan at most 32 valid frame headers without
// decoding PCM and extrapolate encoded byte time over the audio range. The
// byte-work bound counts every consumed byte, garbage included, so a long
// junk gap cannot turn track open into a whole-file read.
void Mp3Decoder::estimateDuration(Impl& d) {
  const uint64_t audioBytes = d.logicalEnd - d.audioStart;
  uint64_t scannedBytes = 0;   // valid encoded frames, drives the rate
  uint64_t scannedSamples = 0;
  uint64_t workBytes = 0;      // every consumed byte, bounds the read work
  for (int frames = 0; frames < 32 && workBytes <= 256 * 1024u;) {
    refillWindow(d);
    if (d.inBufUsed < 4) break;  // end of audio or I/O failure
    mp3dec_frame_info_t info{};
    int samples = mp3dec_decode_frame(&d.dec, d.inBuf, static_cast<int>(d.inBufUsed),
                                      nullptr, &info);
    if (info.frame_bytes <= 0) break;
    if (info.frame_bytes > static_cast<int>(d.inBufUsed)) break;
    size_t used = static_cast<size_t>(info.frame_bytes);
    d.inBufUsed -= used;
    std::memmove(d.inBuf, d.inBuf + used, d.inBufUsed);
    workBytes += used;
    if (samples <= 0) continue;  // garbage or failed frame: work, no rate
    scannedBytes += used;
    scannedSamples += static_cast<uint64_t>(samples);
    frames++;
  }
  MP3_DIAG("[audio] mp3 estimate scanned %u valid / %u total bytes\n",
           static_cast<unsigned>(scannedBytes),
           static_cast<unsigned>(workBytes));
  if (scannedBytes == 0 || d.fmt.sampleRate == 0) return;
  uint64_t ms = audioBytes * scannedSamples * 1000 / (scannedBytes * d.fmt.sampleRate);
  d.fmt.durationMs = static_cast<uint32_t>(ms > 0xFFFFFFFFull ? 0xFFFFFFFFull : ms);
}

// Wind back to the first MPEG frame after the estimation scan and retain the
// first playable PCM.
bool Mp3Decoder::restoreToStart(Impl& d) {
  if (!d.in->seek(static_cast<uint32_t>(d.audioStart))) return false;
  d.inBufUsed = 0;
  d.inputEof = false;
  d.ioFailed = false;
  d.leftoverFrames = 0;
  d.leftoverOffset = 0;
  mp3dec_init(&d.dec);
  DecodeStatus st = DecodeStatus::Ok;
  size_t consumed = 0;
  return frameStep(d, consumed, st, true, nullptr);
}

#ifdef UNIT_TEST
bool Mp3Decoder::openWithFile(DecoderInput& input) { return openInput(input); }

bool Mp3Decoder::open(const char* path) {
  if (!path) return false;
  // The decoder keeps the adapter alive for its own lifetime, mirroring the
  // device path; openWithFile stays borrowed for caller-owned fixtures.
  auto* input = new (std::nothrow) Mp3TestFile();
  if (!input || !input->openFile(path)) { delete input; return false; }
  if (!openInput(*input)) { input->close(); delete input; return false; }
  impl_->ownsInput = true;
  return true;
}
#else
bool Mp3Decoder::open(const char* path) {
  if (!path) return false;
  auto* in = new (std::nothrow) SdInput();
  if (!in) return false;
  in->file = SD.open(path, FILE_READ);
  if (!in->valid()) { delete in; return false; }
  if (!openInput(*in)) { in->close(); delete in; return false; }
  impl_->ownsInput = true;
  return true;
}
#endif

void Mp3Decoder::close() {
  if (!impl_) return;
  if (impl_->ownsInput && impl_->in) impl_->in->close();
  if (impl_->ownsInput) delete impl_->in;
  delete impl_;
  impl_ = nullptr;
}

AudioFormat Mp3Decoder::format() const {
  return impl_ ? impl_->fmt : AudioFormat{};
}

DecodeStatus Mp3Decoder::decode(int16_t* outStereo, size_t maxFrames, size_t* gotFrames) {
  if (!impl_ || !gotFrames || (!outStereo && maxFrames > 0)) return DecodeStatus::Error;
  return pump(*impl_, outStereo, maxFrames, gotFrames);
}

bool Mp3Decoder::seekMs(uint32_t ms) {
  if (!impl_ || !impl_->formatValid || impl_->fmt.durationMs == 0) return false;
  Impl& d = *impl_;

  uint32_t audioBytes = static_cast<uint32_t>(d.logicalEnd - d.audioStart);
  uint32_t rel = mp3SeekByteOffset(d.info, audioBytes, d.fmt.durationMs, ms);
  uint32_t target = static_cast<uint32_t>(d.audioStart + rel);
  // ponytail: bounded byte pre-roll (one input window before the target)
  // over an approximate TOC/linear map; an indexed reservoir-aware sample
  // seek is the upgrade when measured accuracy requires it.
  // Pre-roll from one window before the target, never before the start.
  uint32_t start = static_cast<uint32_t>(d.audioStart);
  if (ms > 0 && target - d.audioStart > cfg::kCompressedBuf) {
    start = target - cfg::kCompressedBuf;
  }

  // Position the file first; keep the old state when the seek fails.
  if (!d.in->seek(start)) return false;
  d.inBufUsed = 0;
  d.inputEof = false;
  d.ioFailed = false;
  d.leftoverFrames = 0;
  d.leftoverOffset = 0;
  mp3dec_init(&d.dec);
  uint64_t clampedMs = ms > d.fmt.durationMs ? d.fmt.durationMs : ms;
  d.samplesDelivered = clampedMs * d.fmt.sampleRate / 1000;
  d.prerollActive = ms > 0;
  d.prerollTarget = target;
  d.warmupSynth = 0;
  return true;
}

uint32_t Mp3Decoder::positionMs() const {
  if (!impl_ || impl_->fmt.sampleRate == 0) return 0;
  return static_cast<uint32_t>(impl_->samplesDelivered * 1000 / impl_->fmt.sampleRate);
}
