// 모달 대화상자 공용 입력 헬퍼.
#pragma once
#include <windows.h>

inline int dlgClamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline int dlgReadInt(HWND e, int dft) {
  wchar_t buf[16]; int n = GetWindowTextW(e, buf, 15);
  int v = 0; bool any = false;
  for (int i = 0; i < n; i++)
    if (buf[i] >= L'0' && buf[i] <= L'9') { v = v * 10 + (buf[i] - L'0'); any = true; }
  return any ? v : dft;
}
