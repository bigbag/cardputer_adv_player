#pragma once

#include <cstddef>
#include <cstdint>

// Xing/Info sidecar metadata parsed from the first MPEG frame.
struct Mp3StreamInfo {
  uint32_t durationMs = 0;   // frame-count duration; 0 when unknown
  uint32_t totalFrames = 0;  // 0 when the frames field is absent
  uint32_t audioBytes = 0;   // 0 when the bytes field is absent
  bool hasVbrInfo = false;   // recognized Xing or Info tag
  bool hasSeekToc = false;   // usable 100-entry TOC (needs the bytes field)
  uint8_t toc[100]{};
};

// Leading ID3v2 tag size from the first stream bytes. Returns true with
// offset zero for a non-ID3 header, false for a recognized but truncated or
// invalid ID3v2 header (unsupported version, bad synchsafe size, or a tag
// that ends past fileSize).
bool mp3Id3DataStart(const uint8_t* header, size_t size, uint32_t fileSize,
                     uint32_t& dataStart);

// Parse Xing/Info metadata from the first validated MPEG frame bytes (the
// frame starts at frame[0]). All reads stay within the given span. Missing
// or truncated optional fields are absent from the result.
Mp3StreamInfo mp3ParseInfo(const uint8_t* frame, size_t frameSize);

// Return an approximate byte offset from the first MPEG frame.
// Return zero when the target is zero or the duration is unknown.
// Return the audio-range end when the target reaches or exceeds the duration.
// audioBytes must describe the validated audio range.
// Exclude the leading ID3 tag from this range.
uint32_t mp3SeekByteOffset(const Mp3StreamInfo& info, uint32_t audioBytes,
                           uint32_t durationMs, uint32_t targetMs);