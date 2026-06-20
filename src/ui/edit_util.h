// EDIT 컨트롤 본문/선택 취득 공용 헬퍼 (Editor/TableEditor 공유).
#pragma once
#include <windows.h>

#include <string>

inline std::wstring editGetTextW(HWND edit) {
  int len = GetWindowTextLengthW(edit);
  std::wstring w;
  w.resize(len + 1);
  int got = GetWindowTextW(edit, &w[0], len + 1);
  w.resize(got);
  return w;
}
inline void editGetSel(HWND edit, DWORD& a, DWORD& b) {
  SendMessageW(edit, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
}
