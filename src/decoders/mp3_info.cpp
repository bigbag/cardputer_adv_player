#include "mp3_info.hpp"

namespace {

uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

bool id3FooterFlag(uint8_t version, uint8_t flags) {
  // Only ID3v2.4 defines the optional 10-byte footer.
  return version == 4 && (flags & 0x10) != 0;
}

}  // namespace

bool mp3Id3DataStart(const uint8_t* header, size_t size, uint32_t fileSize,
                     uint32_t& dataStart) {
  dataStart = 0;
  if (size < 3) return true;  // too short to recognize an ID3 header
  if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') return true;
  if (size < 10) return false;  // recognized but truncated

  uint8_t version = header[3];
  if (version != 2 && version != 3 && version != 4) return false;
  uint8_t flags = header[5];

  uint32_t sz = (uint32_t(header[6]) << 21) | (uint32_t(header[7]) << 14) |
                (uint32_t(header[8]) << 7) | uint32_t(header[9]);
  if ((header[6] | header[7] | header[8] | header[9]) & 0x80) return false;
  sz += 10;
  if (id3FooterFlag(version, flags)) sz += 10;
  if (sz > fileSize) return false;

  dataStart = sz;
  return true;
}

Mp3StreamInfo mp3ParseInfo(const uint8_t* frame, size_t frameSize) {
  Mp3StreamInfo out;
  if (frameSize < 4) return out;
  if (frame[0] != 0xFF || (frame[1] & 0xE0) != 0xE0) return out;

  uint8_t versionBits = (frame[1] >> 3) & 3;  // 0=2.5, 2=2, 3=1
  uint8_t layerBits = (frame[1] >> 1) & 3;    // 1 = Layer III
  if (versionBits == 1 || layerBits != 1) return out;

  uint8_t rateIndex = (frame[2] >> 2) & 3;
  if (rateIndex == 3) return out;
  static const uint16_t kHz[3][4] = {
      {11025, 12000, 8000, 0},   // MPEG 2.5
      {22050, 24000, 16000, 0},  // MPEG 2
      {44100, 48000, 32000, 0},  // MPEG 1
  };
  // Version bits 0, 2, 3 map to rows 0, 1, 2.
  const uint16_t* row = kHz[versionBits == 3 ? 2 : (versionBits == 2 ? 1 : 0)];
  uint32_t hz = row[rateIndex];
  if (hz == 0) return out;

  size_t off = 4;
  if ((frame[1] & 1) == 0) off += 2;  // optional CRC
  bool mono = ((frame[3] >> 6) & 3) == 3;
  off += versionBits == 3 ? (mono ? 17u : 32u) : (mono ? 9u : 17u);

  if (off + 8 > frameSize) return out;
  bool isXing = frame[off] == 'X' && frame[off + 1] == 'i' &&
                frame[off + 2] == 'n' && frame[off + 3] == 'g';
  bool isInfo = frame[off] == 'I' && frame[off + 1] == 'n' &&
                frame[off + 2] == 'f' && frame[off + 3] == 'o';
  if (!isXing && !isInfo) return out;
  out.hasVbrInfo = true;

  uint32_t flags = be32(frame + off + 4);
  size_t p = off + 8;

  bool tocPresent = false;
  // Fields are sequential; a field that does not fit the span is absent,
  // but any already-parsed field still counts.
  if (flags & 0x1) {
    if (p + 4 <= frameSize) {
      out.totalFrames = be32(frame + p);
      p += 4;
    }
  }
  if (flags & 0x2) {
    if (p + 4 <= frameSize) {
      out.audioBytes = be32(frame + p);
      p += 4;
    }
  }
  if (flags & 0x4) {
    if (p + 100 <= frameSize) {
      for (int i = 0; i < 100; i++) out.toc[i] = frame[p + i];
      tocPresent = true;
      p += 100;
    }
  }

  // A usable TOC needs the byte range it interpolates over and a
  // nondecreasing table.
  out.hasSeekToc = tocPresent && out.audioBytes > 0;
  if (out.hasSeekToc) {
    for (int i = 1; i < 100; i++) {
      if (out.toc[i] < out.toc[i - 1]) {
        out.hasSeekToc = false;
        break;
      }
    }
  }

  if (out.totalFrames > 0) {
    uint32_t samplesPerFrame = versionBits == 3 ? 1152 : 576;
    uint64_t ms = uint64_t(out.totalFrames) * samplesPerFrame * 1000 / hz;
    out.durationMs = ms > 0xFFFFFFFFull ? 0xFFFFFFFFull : ms;
  }
  return out;
}

uint32_t mp3SeekByteOffset(const Mp3StreamInfo& info, uint32_t audioBytes,
                           uint32_t durationMs, uint32_t targetMs) {
  if (targetMs == 0 || durationMs == 0 || audioBytes == 0) return 0;
  if (targetMs >= durationMs) return audioBytes;  // decoder treats this as EOF

  if (info.hasSeekToc && info.audioBytes > 0) {
    // Piecewise-linear interpolation over the 100 entries in 1/256 units of
    // the audio range; 256 is the virtual endpoint after entry 99.
    uint64_t f256 = (uint64_t)targetMs * 256 / durationMs;  // 0..255
    uint32_t i0 = static_cast<uint32_t>(f256 * 100 / 256);  // 0..99
    uint32_t frac = static_cast<uint32_t>(f256 * 100 - (uint64_t)i0 * 256);
    uint32_t next = i0 < 99 ? info.toc[i0 + 1] : 256;
    uint64_t bytePct = (uint64_t)info.toc[i0] * 256 + (next - info.toc[i0]) * frac;
    uint64_t offset = bytePct * audioBytes / (256 * 256);
    return offset <= audioBytes ? static_cast<uint32_t>(offset) : audioBytes;
  }
  return static_cast<uint32_t>((uint64_t)audioBytes * targetMs / durationMs);
}
