// 프레임리스 창의 커스텀 상단바: 버튼 레이아웃/페인트/히트테스트.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "model/Theme.h"

static const int kTopbarH = 38;

// type: 0 텍스트, 1 창제어, 2 닫기, 3 보기세그
struct TopBtn {
  int id;
  std::wstring label;
  RECT rc;
  int type;
};

class Topbar {
 public:
  // 버튼 배치를 (재)계산한다. 텍스트 폭 측정에 uiFont 사용. dpi 로 픽셀 스케일.
  void layout(HWND hwnd, int width, HFONT uiFont, UINT dpi);
  // 상단바 전체를 그린다. dirty/name/view/maximized 는 App 상태. dpi 로 픽셀
  // 스케일.
  void paint(HDC dc, int width, const Theme& theme, HFONT uiFont,
             HFONT glyphFont, bool dirty, const std::wstring& name, int view,
             bool maximized, UINT dpi);

  int btnAt(int x, int y) const;  // 좌표의 버튼 인덱스(없으면 -1)
  int count() const { return (int)btns_.size(); }
  int btnId(int idx) const { return btns_[idx].id; }

  int hot() const { return hot_; }
  void setHot(int i) { hot_ = i; }

 private:
  int leftmostBtnX(int width) const;
  std::vector<TopBtn> btns_;
  int hot_ = -1;
};
