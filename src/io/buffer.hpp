// src/io/buffer.hpp
//
// Gestión de buffers para lectura/escritura eficiente de archivos.

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>

namespace io {

// Buffer dinámico con re-asignación ajustada a tamaños de página
class Buffer {
public:
    explicit Buffer(size_t initial_size = 64 * 1024)
        : data_(new char[initial_size]), capacity_(initial_size), size_(0) {}

    ~Buffer() { delete[] data_; }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    char* data() { return data_; }
    const char* data() const { return data_; }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

    void clear() { size_ = 0; }

    void push_back(char c) {
        if (size_ == capacity_) grow();
        data_[size_++] = c;
    }

    void append(const char* src, size_t n) {
        if (size_ + n > capacity_) grow(n);
        std::memcpy(data_ + size_, src, n);
        size_ += n;
    }

    std::string str() const { return std::string(data_, size_); }

private:
    char* data_;
    size_t capacity_;
    size_t size_;

    void grow(size_t extra = 1) {
        size_t new_cap = capacity_ * 2;
        if (new_cap < capacity_ + extra) new_cap = capacity_ + extra;
        char* new_data = new char[new_cap];
        std::memcpy(new_data, data_, size_);
        delete[] data_;
        data_ = new_data;
        capacity_ = new_cap;
    }
};

} // namespace io
