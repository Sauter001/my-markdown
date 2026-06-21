// EDIT 컨트롤 본문/선택 취득 공용 헬퍼 (Editor/TableEditor 공유).
#pragma once
#include <windows.h>

#include <string>

// RichEdit 는 줄바꿈을 내부적으로 단일 CR(1문자)로 세지만 GetWindowText 는
// CRLF 로 돌려준다. 위치 계산(EM_SETSEL 등)이 내부와 정합하도록 LF 단일 문자로
// 정규화한다(CRLF -> \n, 단독 \r -> \n).
inline std::wstring normalizeLF(const std::wstring& s) {
  std::wstring o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == L'\r') {
      o += L'\n';
      if (i + 1 < s.size() && s[i + 1] == L'\n') i++;  // CRLF -> \n
    } else {
      o += s[i];
    }
  }
  return o;
}
inline std::wstring editGetTextW(HWND edit) {
  int len = GetWindowTextLengthW(edit);
  std::wstring w;
  w.resize(len + 1);
  int got = GetWindowTextW(edit, &w[0], len + 1);
  w.resize(got);
  return normalizeLF(w);
}
inline void editGetSel(HWND edit, DWORD& a, DWORD& b) {
  SendMessageW(edit, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
}
