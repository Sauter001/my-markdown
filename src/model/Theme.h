// 테마 색 해석 (system 모드면 레지스트리로 다크/라이트 판정).
#pragma once
#include <windows.h>

#include <string>

class Theme {
 public:
  explicit Theme(const std::string& mode = "system") : mode_(mode) {}
  void setMode(const std::string& mode) { mode_ = mode; }

  bool isDark() const;

  COLORREF bg() const {
    return isDark() ? RGB(0x0d, 0x11, 0x17) : RGB(0xff, 0xff, 0xff);
  }
  COLORREF fg() const {
    return isDark() ? RGB(0xe6, 0xed, 0xf3) : RGB(0x1f, 0x23, 0x28);
  }
  COLORREF topbarBg() const {
    return isDark() ? RGB(0x16, 0x1b, 0x22) : RGB(0xf6, 0xf8, 0xfa);
  }
  COLORREF border() const {
    return isDark() ? RGB(0x30, 0x36, 0x3d) : RGB(0xe1, 0xe4, 0xe8);
  }
  COLORREF btnHover() const {
    return isDark() ? RGB(0x30, 0x36, 0x3d) : RGB(0xee, 0xf1, 0xf4);
  }
  COLORREF muted() const {
    return isDark() ? RGB(0x8b, 0x94, 0x9e) : RGB(0x6e, 0x77, 0x81);
  }
  COLORREF accent() const {
    return isDark() ? RGB(0x2f, 0x81, 0xf7) : RGB(0x09, 0x69, 0xda);
  }

 private:
  std::string mode_;  // system/light/dark
};
