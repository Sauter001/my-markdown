// RAII 리소스 래퍼: GDI 오브젝트와 커널 HANDLE 소유.
#pragma once
#include <windows.h>

// GDI 오브젝트(HFONT/HBRUSH/HPEN/...) 소유. 소멸 시 DeleteObject.
template <class T>
class GdiHandle {
 public:
  GdiHandle() = default;
  explicit GdiHandle(T h) : h_(h) {}
  GdiHandle(GdiHandle&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
  GdiHandle& operator=(GdiHandle&& o) noexcept {
    if (this != &o) {
      reset();
      h_ = o.h_;
      o.h_ = nullptr;
    }
    return *this;
  }
  GdiHandle(const GdiHandle&) = delete;
  GdiHandle& operator=(const GdiHandle&) = delete;
  ~GdiHandle() { reset(); }
  void reset(T h = nullptr) {
    if (h_) DeleteObject((HGDIOBJ)h_);
    h_ = h;
  }
  T get() const { return h_; }
  explicit operator bool() const { return h_ != nullptr; }

 private:
  T h_ = nullptr;
};
using FontHandle = GdiHandle<HFONT>;
using BrushHandle = GdiHandle<HBRUSH>;
using PenHandle = GdiHandle<HPEN>;

// 커널 HANDLE(파일/프로세스) 소유. 소멸 시 CloseHandle.
class UniqueHandle {
 public:
  UniqueHandle() = default;
  explicit UniqueHandle(HANDLE h) : h_(h) {}
  UniqueHandle(UniqueHandle&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }
  UniqueHandle& operator=(UniqueHandle&& o) noexcept {
    if (this != &o) {
      reset();
      h_ = o.h_;
      o.h_ = nullptr;
    }
    return *this;
  }
  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;
  ~UniqueHandle() { reset(); }
  void reset(HANDLE h = nullptr) {
    if (h_ && h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
    h_ = h;
  }
  HANDLE get() const { return h_; }
  bool valid() const { return h_ != nullptr && h_ != INVALID_HANDLE_VALUE; }

 private:
  HANDLE h_ = nullptr;
};
