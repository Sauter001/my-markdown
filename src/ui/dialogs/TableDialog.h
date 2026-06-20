// 표 삽입 모달 대화상자 (열/행 입력).
#pragma once
#include <windows.h>

class TableDialog {
public:
  // 모달 표시. 확인 시 cols/rows 를 채우고 true 반환.
  bool show(HWND parent, HFONT uiFont, int &cols, int &rows);

private:
  static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
  bool done_ = false;
  bool ok_ = false;
};
