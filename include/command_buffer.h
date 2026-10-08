#pragma once

#include <cstddef>
#include <cstring>

template <std::size_t Capacity>
class CommandBuffer {
  static_assert(Capacity > 0, "CommandBuffer capacity must be greater than zero");

 public:
  CommandBuffer() { clear(); }

  bool set(const char* text) {
    if (text == nullptr) {
      clear();
      return false;
    }

    const std::size_t length = std::strlen(text);
    if (length > Capacity) return false;

    std::memcpy(data_, text, length);
    data_[length] = '\0';
    length_ = length;
    cursor_ = length;
    return true;
  }

  bool set(const char* text, std::size_t length) {
    if (text == nullptr && length != 0) return false;
    if (length > Capacity) return false;

    if (length != 0) std::memcpy(data_, text, length);
    data_[length] = '\0';
    length_ = length;
    cursor_ = length;
    return true;
  }

  void clear() {
    data_[0] = '\0';
    length_ = 0;
    cursor_ = 0;
  }

  bool insert(char value) {
    if (length_ >= Capacity) return false;

    for (std::size_t index = length_; index > cursor_; --index) {
      data_[index] = data_[index - 1];
    }
    data_[cursor_] = value;
    ++length_;
    ++cursor_;
    data_[length_] = '\0';
    return true;
  }

  bool insert(const char* text) {
    if (text == nullptr) return false;
    return insert(text, std::strlen(text));
  }

  bool insert(const char* text, std::size_t length) {
    if (text == nullptr && length != 0) return false;
    if (length == 0) return true;
    if (length_ > Capacity - length) return false;

    for (std::size_t index = length_; index > cursor_; --index) {
      data_[index + length - 1] = data_[index - 1];
    }
    if (length != 0) std::memcpy(data_ + cursor_, text, length);

    length_ += length;
    cursor_ += length;
    data_[length_] = '\0';
    return true;
  }

  bool backspace() {
    if (cursor_ == 0) return false;

    const std::size_t eraseIndex = cursor_ - 1;
    for (std::size_t index = eraseIndex; index < length_; ++index) {
      data_[index] = data_[index + 1];
    }
    --cursor_;
    --length_;
    return true;
  }

  bool deleteForward() {
    if (cursor_ >= length_) return false;

    for (std::size_t index = cursor_; index < length_; ++index) {
      data_[index] = data_[index + 1];
    }
    --length_;
    return true;
  }

  void moveLeft() {
    if (cursor_ > 0) --cursor_;
  }

  void moveRight() {
    if (cursor_ < length_) ++cursor_;
  }

  void moveHome() { cursor_ = 0; }

  void moveEnd() { cursor_ = length_; }

  void setCursor(std::size_t position) {
    cursor_ = position <= length_ ? position : length_;
  }

  const char* c_str() const { return data_; }
  std::size_t length() const { return length_; }
  std::size_t cursor() const { return cursor_; }
  std::size_t capacity() const { return Capacity; }

  bool invariantHolds() const {
    return length_ <= Capacity &&
           cursor_ <= length_ &&
           data_[length_] == '\0';
  }

 private:
  char data_[Capacity + 1] = {};
  std::size_t length_ = 0;
  std::size_t cursor_ = 0;
};
