#pragma once

#include <cstddef>
#include <cstdint>

struct DecoderInput {
  virtual size_t read(uint8_t* dst, size_t bytes) = 0;
  virtual bool seek(uint32_t pos) = 0;
  virtual uint32_t position() = 0;
  virtual uint32_t size() = 0;
  virtual bool valid() = 0;
  virtual void close() = 0;
  virtual ~DecoderInput() = default;
};

#ifndef UNIT_TEST
#include <FS.h>

struct SdInput : DecoderInput {
  fs::File file;
  size_t read(uint8_t* dst, size_t bytes) override { return file.read(dst, bytes); }
  bool seek(uint32_t pos) override { return file.seek(pos); }
  uint32_t position() override { return file.position(); }
  uint32_t size() override { return file.size(); }
  bool valid() override { return static_cast<bool>(file); }
  void close() override { file.close(); }
};
#endif
