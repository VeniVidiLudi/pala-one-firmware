#ifndef PALA_PURE_STREAM_H
#define PALA_PURE_STREAM_H

#include "arduino_compat.h"

// Read-only byte stream interface. Replaces direct use of Arduino `File` in
// pure modules so they can be tested off-device with in-memory streams.
struct IReadStream {
  virtual ~IReadStream() = default;

  // Read one byte and advance the position. Returns -1 at EOF.
  virtual int read() = 0;

  // Move the read cursor to absolute byte position `pos`. Returns true if
  // the position is within the stream.
  virtual bool seek(uint32_t pos) = 0;

  // Current absolute byte position.
  virtual uint32_t position() = 0;

  // Total stream size in bytes.
  virtual size_t size() = 0;

  // Number of bytes remaining from the current position.
  virtual bool available() = 0;
};

// In-memory stream backed by a byte buffer or String. Test-friendly.
class StringReadStream : public IReadStream {
public:
  explicit StringReadStream(const String& data) : data_(data) {}
  explicit StringReadStream(const char* data) : data_(data) {}

  int read() override {
    if (pos_ >= data_.length()) return -1;
    return (uint8_t)data_[pos_++];
  }
  bool seek(uint32_t p) override {
    if (p > data_.length()) return false;
    pos_ = p;
    return true;
  }
  uint32_t position() override { return pos_; }
  size_t size() override { return data_.length(); }
  bool available() override { return pos_ < data_.length(); }

private:
  String data_;
  uint32_t pos_ = 0;
};

#endif  // PALA_PURE_STREAM_H
