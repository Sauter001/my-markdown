#include "ui/Editor.h"

#include <vector>

#include "core/markdown.h"
#include "core/str_util.h"
#include "model/Settings.h"
#include "ui/edit_util.h"

static HFONT makeEditFont(int px) {
  return CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
}
// 줌(%)을 반영한 에디터 글꼴 픽셀. 과도한 값은 6-72px 로 제한.
static int effectiveFontPx(const Settings& s) {
  long px = (long)s.fontSize * s.zoom / 100;
  if (px < 6) px = 6;
  if (px > 72) px = 72;
  return (int)px;
}

void Editor::create(HWND parent, const Settings& settings) {
  settings_ = &settings;
  DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                ES_NOHIDESEL | ES_WANTRETURN | ES_AUTOVSCROLL;
  if (!settings.wrap) style |= ES_AUTOHSCROLL | WS_HSCROLL;
  edit_ =
      CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 0, 0, parent,
                      (HMENU)(INT_PTR)1, GetModuleHandleW(nullptr), nullptr);
  SendMessageW(edit_, EM_SETLIMITTEXT, (WPARAM)0x7FFFFFFE, 0);
  applyStyle();
  SetWindowLongPtrW(edit_, GWLP_USERDATA, (LONG_PTR)this);  // proc thunk 용
  orig_ =
      (WNDPROC)SetWindowLongPtrW(edit_, GWLP_WNDPROC, (LONG_PTR)&Editor::proc);
}

void Editor::recreateForWrap(HWND parent) {
  if (!edit_) return;
  std::wstring text = getTextW();
  DWORD a, b;
  editGetSel(edit_, a, b);
  DestroyWindow(edit_);
  create(parent, *settings_);
  suppress_ = true;
  SetWindowTextW(edit_, text.c_str());
  suppress_ = false;
  SendMessageW(edit_, EM_SETSEL, (WPARAM)a, (LPARAM)b);
  SendMessageW(edit_, EM_SCROLLCARET, 0, 0);
}

std::wstring Editor::getTextW() const { return editGetTextW(edit_); }
std::string Editor::getTextUtf8Lf() const {
  return crlfToLF(wide_to_utf8(editGetTextW(edit_)));  // 파일에는 LF 로 저장
}
void Editor::setTextUtf8Lf(const std::string& utf8lf) {
  std::wstring w = utf8_to_wide(lfToCRLF(utf8lf));  // EDIT 에는 CRLF 로
  suppress_ = true;
  SetWindowTextW(edit_, w.c_str());
  suppress_ = false;
}

void Editor::applyStyle() {
  font_.reset(makeEditFont(effectiveFontPx(*settings_)));
  SendMessageW(edit_, WM_SETFONT, (WPARAM)font_.get(), TRUE);
  DWORD tw = (DWORD)(settings_->tabSize * 4 * settings_->zoom /
                     100);  // 대략 N칸(다이얼로그 단위), 줌 반영
  if (tw < 1) tw = 1;
  SendMessageW(edit_, EM_SETTABSTOPS, 1, (LPARAM)&tw);
  SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
               MAKELONG(10, 10));
}

// 선택 범위를 줄 단위로 들여쓰기/내어쓰기
void Editor::blockIndent(const std::wstring& text, DWORD a, DWORD b,
                         bool shift) {
  int tabSize = settings_->tabSize;
  DWORD ls = a;
  while (ls > 0 && text[ls - 1] != L'\n') ls--;
  DWORD le = b;
  if (le > a && le > 0 && text[le - 1] == L'\n') le--;
  while (le < text.size() && text[le] != L'\n') le++;
  std::vector<std::wstring> lines = splitLines(text.substr(ls, le - ls));
  std::wstring indent((size_t)tabSize, L' ');
  for (std::wstring& ln : lines) {
    if (!shift)
      ln = indent + ln;
    else if (!ln.empty() && ln[0] == L'\t')
      ln.erase(0, 1);
    else {
      int n = 0;
      while (n < tabSize && n < (int)ln.size() && ln[n] == L' ') n++;
      ln.erase(0, n);
    }
  }
  std::wstring out;
  for (size_t i = 0; i < lines.size(); i++) {
    if (i) out += L"\r\n";
    out += lines[i];
  }
  SendMessageW(edit_, EM_SETSEL, ls, le);
  SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)out.c_str());
  SendMessageW(edit_, EM_SETSEL, ls, ls + (DWORD)out.size());
}

// Tab: 현재 줄이 리스트 항목이면 항목 전체를 들여쓰기/내어쓰기(ordered 번호
// 보정). 리스트가 아니면 캐럿에 공백 N칸. 멀티라인 선택은 블록 들여쓰기.
void Editor::tabIndent(bool shift) {
  int tabSize = settings_->tabSize;
  std::wstring text = getTextW();
  DWORD a, b;
  editGetSel(edit_, a, b);
  if (a != b) {
    bool multiline = false;
    for (DWORD i = a; i < b && i < text.size(); i++)
      if (text[i] == L'\n') {
        multiline = true;
        break;
      }
    if (multiline) {
      blockIndent(text, a, b, shift);
      return;
    }
  }

  DWORD ls = a;
  while (ls > 0 && text[ls - 1] != L'\n') ls--;
  DWORD le = a;
  while (le < text.size() && text[le] != L'\n') le++;
  DWORD lineEnd = le;
  if (lineEnd > ls && text[lineEnd - 1] == L'\r') lineEnd--;
  std::wstring line = text.substr(ls, lineEnd - ls);

  ListItem it;
  if (a == b && parseListItem(line, it)) {
    int oldLen = (int)it.indent.size();
    if (shift && oldLen == 0) return;  // 더 내어쓸 수 없음
    int newLen = shift ? (oldLen - tabSize) : (oldLen + tabSize);
    if (newLen < 0) newLen = 0;

    std::vector<std::wstring> lines = splitLines(text);
    int curIdx = 0;
    for (DWORD i = 0; i < a && i < text.size(); i++)
      if (text[i] == L'\n') curIdx++;

    std::wstring newIndent((size_t)newLen, L' ');
    std::wstring newLine;
    if (it.ordered) {
      int num = orderedStartNum(lines, curIdx, newLen);
      newLine = newIndent + std::to_wstring(num) + std::wstring(1, it.delim) +
                L" " + it.rest;
    } else {
      newLine =
          newIndent +
          line.substr(oldLen);  // 마커/내용/체크박스 그대로, 들여쓰기만 변경
    }
    LRESULT caret =
        (LRESULT)a + ((LRESULT)newLine.size() - (LRESULT)line.size());
    if (caret < (LRESULT)ls) caret = ls;
    SendMessageW(edit_, EM_SETSEL, ls, lineEnd);
    SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)newLine.c_str());
    SendMessageW(edit_, EM_SETSEL, (WPARAM)caret, (LPARAM)caret);
    return;
  }

  if (!shift) {
    std::wstring sp((size_t)tabSize, L' ');
    SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)sp.c_str());
    return;
  }
  blockIndent(text, a, b, true);  // Shift+Tab 비리스트: 현재 줄 내어쓰기
}

// Enter: 목록 항목이면 같은 마커로 이어쓰고, 빈 항목이면 마커를 제거(리스트
// 종료).
bool Editor::listEnter() {
  DWORD a, b;
  editGetSel(edit_, a, b);
  if (a != b) return false;
  std::wstring text = getTextW();
  DWORD ls = a;
  while (ls > 0 && text[ls - 1] != L'\n') ls--;
  DWORD le = a;
  while (le < text.size() && text[le] != L'\n') le++;
  std::wstring line = text.substr(ls, le - ls);
  if (!line.empty() && line.back() == L'\r') line.pop_back();

  size_t i = 0;
  while (i < line.size() && (line[i] == L' ' || line[i] == L'\t')) i++;
  std::wstring indent = line.substr(0, i);
  auto endList = [&]() {
    SendMessageW(edit_, EM_SETSEL, ls, le);
    SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)L"");
  };
  auto cont = [&](const std::wstring& marker) {
    std::wstring ins = L"\r\n" + marker;
    SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)ins.c_str());
  };

  if (i < line.size() &&
      (line[i] == L'-' || line[i] == L'*' || line[i] == L'+')) {
    wchar_t marker = line[i];
    size_t k = i + 1;
    if (k < line.size() && line[k] == L' ') {
      while (k < line.size() && line[k] == L' ') k++;
      if (k + 2 < line.size() && line[k] == L'[' &&
          (line[k + 1] == L' ' || line[k + 1] == L'x' || line[k + 1] == L'X') &&
          line[k + 2] == L']') {
        size_t r = k + 3;
        if (r < line.size() && line[r] == L' ') r++;
        if (rtrimWs(line.substr(r)).empty()) {
          endList();
          return true;
        }
        cont(indent + std::wstring(1, marker) + L" [ ] ");
        return true;
      }
      if (rtrimWs(line.substr(k)).empty()) {
        endList();
        return true;
      }
      cont(indent + std::wstring(1, marker) + L" ");
      return true;
    }
  }
  size_t j = i;
  while (j < line.size() && line[j] >= L'0' && line[j] <= L'9') j++;
  if (j > i && j < line.size() && (line[j] == L'.' || line[j] == L')')) {
    wchar_t delim = line[j];
    size_t k = j + 1;
    if (k < line.size() && line[k] == L' ') {
      while (k < line.size() && line[k] == L' ') k++;
      if (rtrimWs(line.substr(k)).empty()) {
        endList();
        return true;
      }
      int num = 0;
      for (size_t t = i; t < j; t++) num = num * 10 + (line[t] - L'0');
      cont(indent + std::to_wstring(num + 1) + std::wstring(1, delim) + L" ");
      return true;
    }
  }
  return false;
}

LRESULT Editor::onMessage(HWND e, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_KEYDOWN:
      if (w == VK_TAB) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (onTableNav && onTableNav(shift)) {
          swallowChar_ = true;
          return 0;
        }  // 표 우선
        tabIndent(shift);
        swallowChar_ = true;
        return 0;
      }
      if (w == VK_RETURN) {
        if (onTableEnter && onTableEnter()) {
          swallowChar_ = true;
          return 0;
        }  // 표 우선
        if (listEnter()) {
          swallowChar_ = true;
          return 0;
        }
      }
      if (w == VK_UP || w == VK_DOWN || w == VK_PRIOR || w == VK_NEXT ||
          w == VK_HOME || w == VK_END) {  // 키보드 스크롤
        LRESULT r = CallWindowProcW(orig_, e, m, w, l);
        if (onScroll) onScroll();
        return r;
      }
      break;
    case WM_CHAR:
      if (swallowChar_) {
        swallowChar_ = false;
        return 0;
      }
      break;
    case WM_VSCROLL:
    case WM_MOUSEWHEEL: {  // 스크롤바/휠
      LRESULT r = CallWindowProcW(orig_, e, m, w, l);
      if (onScroll) onScroll();
      return r;
    }
  }
  return CallWindowProcW(orig_, e, m, w, l);
}

LRESULT CALLBACK Editor::proc(HWND e, UINT m, WPARAM w, LPARAM l) {
  Editor* self = (Editor*)GetWindowLongPtrW(e, GWLP_USERDATA);
  if (!self) return DefWindowProcW(e, m, w, l);
  return self->onMessage(e, m, w, l);
}
