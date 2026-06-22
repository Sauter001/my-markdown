#include "ui/Editor.h"

#define _RICHEDIT_VER 0x0500
#include <richedit.h>

#include <cmath>
#include <vector>

// MinGW richedit.h 에 없어 직접 정의. POINT(픽셀) 단위로 스크롤 위치를
// 읽고/설정한다(줄 단위가 아닌 픽셀 정밀도라 부드러운 스크롤에 사용).
#ifndef EM_GETSCROLLPOS
#define EM_GETSCROLLPOS (WM_USER + 221)
#define EM_SETSCROLLPOS (WM_USER + 222)
#endif

static const UINT_PTR IDT_SMOOTHSCROLL = 100;  // 휠 애니메이션 타이머(edit_ 소유)

#include "core/dpi.h"
#include "core/markdown.h"
#include "core/str_util.h"
#include "model/Settings.h"
#include "ui/edit_util.h"

static HFONT makeEditFont(int px) {
  return CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
}
// fontSize(pt)에 줌(%)과 DPI를 반영한 글꼴 픽셀 높이. 과도한 값은 6-200px로
// 제한.
static int effectiveFontPx(const Settings& s, UINT dpi) {
  long px = MulDiv((int)s.fontSize * s.zoom, (int)dpi,
                   7200);  // pt*(zoom/100)*(dpi/72)
  if (px < 6) px = 6;
  if (px > 200) px = 200;
  return (int)px;
}

void Editor::create(HWND parent, const Settings& settings) {
  static HMODULE rich = LoadLibraryW(L"Msftedit.dll");  // RICHEDIT50W 등록(1회)
  (void)rich;
  settings_ = &settings;
  DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                ES_NOHIDESEL | ES_WANTRETURN | ES_AUTOVSCROLL;
  if (!settings.wrap) style |= ES_AUTOHSCROLL | WS_HSCROLL;
  edit_ =
      CreateWindowExW(0, MSFTEDIT_CLASS, L"", style, 0, 0, 0, 0, parent,
                      (HMENU)(INT_PTR)1, GetModuleHandleW(nullptr), nullptr);
  // 평문 모드(단일 글꼴, RTF/자동서식 없음, 다단계 undo). 비어 있을 때 설정.
  SendMessageW(edit_, EM_SETTEXTMODE, (WPARAM)(TM_PLAINTEXT | TM_MULTICODEPAGE),
               0);
  SendMessageW(edit_, EM_EXLIMITTEXT, 0, (LPARAM)0x7FFFFFFE);
  SendMessageW(edit_, EM_SETUNDOLIMIT, (WPARAM)200, 0);  // 다단계 undo 깊이
  SendMessageW(edit_, EM_SETEVENTMASK, 0,
               (LPARAM)ENM_CHANGE);  // EN_CHANGE 수신(더티/렌더)
  applyStyle();
  applyColors(bg_, fg_);  // 마지막/기본 색 적용(재생성 시 복원)
  SetWindowLongPtrW(edit_, GWLP_USERDATA, (LONG_PTR)this);  // proc thunk 용
  orig_ =
      (WNDPROC)SetWindowLongPtrW(edit_, GWLP_WNDPROC, (LONG_PTR)&Editor::proc);
}

void Editor::recreateForWrap(HWND parent) {
  if (!edit_) return;
  stopSmoothScroll();  // 재생성 전 관성 타이머 정리
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
  // 입력이 외부에서 만든 CRLF/CR 파일일 수 있으므로 먼저 LF 로 통일한 뒤
  // CRLF 로 변환한다. 그러지 않으면 CRLF 가 \r\r\n 으로 이중 변환되고,
  // RichEdit 는 \r\r\n 을 개행 없이 통째로 버려 모든 줄이 한 줄로 합쳐진다.
  std::wstring w = utf8_to_wide(lfToCRLF(crlfToLF(utf8lf)));
  suppress_ = true;
  SetWindowTextW(edit_, w.c_str());
  suppress_ = false;
  SendMessageW(edit_, EM_EMPTYUNDOBUFFER, 0, 0);  // 로드는 undo 로 남기지 않음
}

void Editor::applyStyle() {
  font_.reset(makeEditFont(effectiveFontPx(*settings_, dpi::forWindow(edit_))));
  SendMessageW(edit_, WM_SETFONT, (WPARAM)font_.get(), TRUE);
  DWORD tw = (DWORD)(settings_->tabSize * 4 * settings_->zoom /
                     100);  // 대략 N칸(다이얼로그 단위), 줌 반영
  if (tw < 1) tw = 1;
  SendMessageW(edit_, EM_SETTABSTOPS, 1, (LPARAM)&tw);
  SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
               MAKELONG(10, 10));
  // WM_SETFONT 는 RichEdit 의 기본 글자색을 기본값(검정)으로 되돌리므로,
  // 마지막으로 적용한 테마 색을 다시 적용한다(다크 모드에서 글꼴/줌/DPI 변경
  // 시 글자가 검게 보이는 문제 방지).
  applyColors(bg_, fg_);
}

// RichEdit 는 WM_CTLCOLOREDIT 를 보내지 않으므로 배경/글자색을 메시지로 직접
// 적용한다. 색은 멤버에 저장해 wrap 토글 등 재생성 시 복원한다.
void Editor::applyColors(COLORREF bg, COLORREF fg) {
  bg_ = bg;
  fg_ = fg;
  if (!edit_) return;
  SendMessageW(edit_, EM_SETBKGNDCOLOR, 0, (LPARAM)bg);
  CHARFORMAT2W cf = {};
  cf.cbSize = sizeof(cf);
  cf.dwMask = CFM_COLOR;  // 글자색만(글꼴은 WM_SETFONT 가 관리)
  cf.crTextColor = fg;
  SendMessageW(edit_, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
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
    if (i) out += L"\n";  // RichEdit 줄바꿈=1문자(위치 정합)
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
    std::wstring ins = L"\n" + marker;  // RichEdit 줄바꿈=1문자
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

// 헤더를 스캔해 목차(중첩 링크 목록)를 캐럿 위치에 삽입한다. 헤더가 없으면
// 아무것도 하지 않는다. 링크 slug 는 미리보기 헤더 id 와 일치한다(클릭 이동).
void Editor::insertToc() {
  std::wstring text = getTextW();  // LF 정규화 본문(위치 정합)
  std::wstring toc = buildTocMarkdown(parseHeadings(text), settings_->tabSize);
  if (toc.empty()) return;  // 헤더 없음
  DWORD pos, selEnd;
  editGetSel(edit_, pos, selEnd);
  bool atStart =
      (pos == 0) || (pos <= (DWORD)text.size() && text[pos - 1] == L'\n');
  std::wstring block = W(u8"## 목차\n") + toc;  // 한글은 런타임 UTF-8 변환(W)
  std::wstring ins = (atStart ? std::wstring() : L"\n") + block + L"\n";
  SendMessageW(edit_, EM_SETSEL, (WPARAM)pos, (LPARAM)selEnd);
  SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)ins.c_str());
  DWORD caret = pos + (DWORD)ins.size();
  SendMessageW(edit_, EM_SETSEL, (WPARAM)caret, (LPARAM)caret);
  SetFocus(edit_);
}

// 입력 문자 자동 페어링: 괄호/따옴표/백틱/별표 짝 삽입, 닫는/대칭 문자 스킵
// 오버, 선택 감싸기, 백틱 코드펜스/별표 굵게 확장. 처리하면 true(기본 입력
// 차단). autoPair 설정이 꺼져 있으면 통과.
bool Editor::autoPair(wchar_t c) {
  if (!settings_->autoPair) return false;
  switch (c) {  // 대상 문자만 처리, 나머지는 통과
    case L'(':
    case L'[':
    case L'{':
    case L')':
    case L']':
    case L'}':
    case L'"':
    case L'\'':
    case L'`':
    case L'*':
      break;
    default:
      return false;
  }

  DWORD a, b;
  editGetSel(edit_, a, b);
  std::wstring text = getTextW();
  bool hasSel = (a != b);
  wchar_t prev = (a > 0 && a <= text.size()) ? text[a - 1] : 0;
  wchar_t next = (a < text.size()) ? text[a] : 0;

  auto isWordChar = [](wchar_t w) -> bool {
    return (w >= L'A' && w <= L'Z') || (w >= L'a' && w <= L'z') ||
           (w >= L'0' && w <= L'9') || w == L'_' ||
           (w >= 0xAC00 && w <= 0xD7A3);  // 한글 음절 가-힣
  };
  auto wrap = [&](wchar_t open, wchar_t close) {
    if (hasSel) {
      std::wstring sel = text.substr(a, b - a);
      std::wstring ins;
      ins += open;
      ins += sel;
      ins += close;
      SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)ins.c_str());
      SendMessageW(edit_, EM_SETSEL, (WPARAM)(a + 1),
                   (LPARAM)(a + 1 + (DWORD)sel.size()));  // 안쪽 재선택
    } else {
      wchar_t ins[3] = {open, close, 0};
      SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)ins);
      SendMessageW(edit_, EM_SETSEL, (WPARAM)(a + 1), (LPARAM)(a + 1));
    }
  };
  auto skipOver = [&]() {  // 삽입 없이 캐럿만 우로 1칸
    SendMessageW(edit_, EM_SETSEL, (WPARAM)(a + 1), (LPARAM)(a + 1));
  };

  switch (c) {
    case L'(':
      wrap(L'(', L')');
      return true;
    case L'[':
      wrap(L'[', L']');
      return true;
    case L'{':
      wrap(L'{', L'}');
      return true;
    case L')':
    case L']':
    case L'}':
      if (!hasSel && next == c) {
        skipOver();
        return true;
      }
      return false;  // 짝이 없으면 일반 입력
    case L'"':
    case L'\'':
      if (!hasSel && next == c) {
        skipOver();
        return true;
      }
      if (!hasSel && isWordChar(prev)) return false;  // 축약형 등 페어링 제외
      wrap(c, c);
      return true;
    case L'`':
      // `|` 에서 다시 ` -> 코드 펜스로 확장. 개행 없이 캐럿을 여는 펜스 끝에 둬
      // 언어 식별자를 바로 입력할 수 있게 한다.
      if (!hasSel && prev == L'`' && next == L'`') {
        SendMessageW(edit_, EM_SETSEL, (WPARAM)(a - 1), (LPARAM)(a + 1));
        SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)L"```\n```");
        DWORD caret = (a - 1) + 3;  // 여는 ``` 바로 뒤(같은 줄)
        SendMessageW(edit_, EM_SETSEL, (WPARAM)caret, (LPARAM)caret);
        return true;
      }
      if (!hasSel && next == L'`') {  // 인라인 코드 닫기
        skipOver();
        return true;
      }
      wrap(L'`', L'`');
      return true;
    case L'*':
      // 코드 블록 안에서는 페어링하지 않는다(C 포인터 `int *p`, 곱셈 `a * b` 등
      // 방해 방지). 마크다운 본문에서는 강조용으로 짝을 만든다.
      if (inCodeBlock(text, a)) return false;
      // *|* 에서 다시 * -> **|** (굵게)
      if (!hasSel && prev == L'*' && next == L'*') {
        SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)L"**");
        SendMessageW(edit_, EM_SETSEL, (WPARAM)(a + 1), (LPARAM)(a + 1));
        return true;
      }
      if (!hasSel && next == L'*') {
        skipOver();
        return true;
      }
      wrap(L'*', L'*');
      return true;
  }
  return false;
}

// 캐럿이 빈 짝 사이면(앞=여는 문자, 뒤=그 짝) 양쪽 함께 삭제. 처리하면 true.
bool Editor::pairBackspace() {
  if (!settings_->autoPair) return false;
  DWORD a, b;
  editGetSel(edit_, a, b);
  if (a != b || a == 0) return false;
  std::wstring text = getTextW();
  if (a > text.size()) return false;
  wchar_t open = text[a - 1];
  wchar_t next = (a < text.size()) ? text[a] : 0;
  wchar_t close = 0;
  switch (open) {
    case L'(':
      close = L')';
      break;
    case L'[':
      close = L']';
      break;
    case L'{':
      close = L'}';
      break;
    case L'"':
      close = L'"';
      break;
    case L'\'':
      close = L'\'';
      break;
    case L'`':
      close = L'`';
      break;
    case L'*':
      close = L'*';
      break;
    default:
      return false;
  }
  if (next != close) return false;
  SendMessageW(edit_, EM_SETSEL, (WPARAM)(a - 1), (LPARAM)(a + 1));
  SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)L"");
  return true;
}

// 줄 [s,e) 가 코드 펜스(``` 또는 ~~~, 3개 이상)로 시작하는지(선행 공백 허용).
static bool isFenceLine(const std::wstring& t, size_t s, size_t e) {
  size_t i = s;
  while (i < e && (t[i] == L' ' || t[i] == L'\t')) i++;
  if (e - i < 3) return false;
  wchar_t c = t[i];
  return (c == L'`' || c == L'~') && t[i + 1] == c && t[i + 2] == c;
}

// pos 가 속한 줄보다 위쪽의 코드 펜스 줄 개수가 홀수면 펜스 코드블록 내부다.
bool Editor::inCodeBlock(const std::wstring& text, DWORD pos) {
  DWORD ls = pos;
  while (ls > 0 && text[ls - 1] != L'\n') ls--;  // 현재 줄 시작
  int fences = 0;
  size_t st = 0;
  while (st < ls) {
    size_t eol = st;
    while (eol < text.size() && text[eol] != L'\n') eol++;
    if (isFenceLine(text, st, eol)) fences++;
    st = eol + 1;
  }
  return (fences & 1) != 0;
}

// Enter: 현재 줄의 선행 공백을 새 줄에 유지(코드 블록 안에서 여는 중괄호 뒤/
// `{|}` 는 한 단계 더 들여쓴다). 선택이 있으면 기본 동작에 맡긴다.
bool Editor::autoIndentEnter() {
  DWORD a, b;
  editGetSel(edit_, a, b);
  if (a != b) return false;
  std::wstring text = getTextW();
  DWORD ls = a;
  while (ls > 0 && text[ls - 1] != L'\n') ls--;
  std::wstring indent;  // 현재 줄 선행 공백(캐럿 이전까지)
  for (DWORD i = ls;
       i < a && i < text.size() && (text[i] == L' ' || text[i] == L'\t'); i++)
    indent += text[i];
  wchar_t prev = (a > 0) ? text[a - 1] : 0;
  wchar_t next = (a < text.size()) ? text[a] : 0;
  std::wstring oneTab((size_t)settings_->tabSize, L' ');
  bool code = inCodeBlock(text, a);
  if (code && prev == L'{' && next == L'}') {  // {|} -> 블록 펼침
    std::wstring ins = L"\n" + indent + oneTab + L"\n" + indent;
    SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)ins.c_str());
    DWORD caret = a + 1 + (DWORD)indent.size() + (DWORD)oneTab.size();
    SendMessageW(edit_, EM_SETSEL, (WPARAM)caret, (LPARAM)caret);
    return true;
  }
  std::wstring ins = L"\n" + indent;
  if (code && prev == L'{') ins += oneTab;  // 여는 중괄호 뒤 한 단계 더
  SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)ins.c_str());
  return true;
}

// 선택(또는 캐럿)이 걸친 라인들의 범위 [start, end]. end 는 마지막 라인의 개행
// 다음(개행 포함), 마지막 줄이면 텍스트 끝. 위치는 LF 기준(RichEdit 내부와 정합).
void Editor::lineRange(DWORD& start, DWORD& end) {
  DWORD a, b;
  editGetSel(edit_, a, b);
  std::wstring text = getTextW();
  DWORD ls = a;
  while (ls > 0 && text[ls - 1] != L'\n') ls--;
  DWORD le = b;
  if (le > a && le > 0 && text[le - 1] == L'\n')
    le--;  // 선택 끝이 줄 첫머리면 그 줄 제외
  while (le < text.size() && text[le] != L'\n') le++;
  if (le < text.size()) le++;  // 개행 포함
  start = ls;
  end = le;
}

void Editor::copyLine() {
  DWORD a, b;
  editGetSel(edit_, a, b);  // 캐럿/선택 보존
  DWORD s, en;
  lineRange(s, en);
  SendMessageW(edit_, EM_SETSEL, (WPARAM)s, (LPARAM)en);
  SendMessageW(edit_, WM_COPY, 0, 0);
  SendMessageW(edit_, EM_SETSEL, (WPARAM)a, (LPARAM)b);  // 복원(텍스트 불변)
}

void Editor::cutLine() {
  DWORD s, en;
  lineRange(s, en);
  std::wstring text = getTextW();
  if (en >= text.size() && s > 0 && text[s - 1] == L'\n')
    s--;  // 마지막 줄: 앞 개행까지 제거
  SendMessageW(edit_, EM_SETSEL, (WPARAM)s, (LPARAM)en);
  SendMessageW(edit_, WM_CUT, 0, 0);  // 클립보드 복사 + 삭제(undo 됨)
}

void Editor::deleteLine() {
  DWORD s, en;
  lineRange(s, en);
  std::wstring text = getTextW();
  if (en >= text.size() && s > 0 && text[s - 1] == L'\n') s--;
  SendMessageW(edit_, EM_SETSEL, (WPARAM)s, (LPARAM)en);
  SendMessageW(edit_, EM_REPLACESEL, TRUE, (LPARAM)L"");  // 클립보드 미사용
}

// 현재 글꼴의 한 줄 픽셀 높이(외부 행간 포함). 휠 한 칸 이동량 계산용.
int Editor::lineHeightPx() const {
  HDC dc = GetDC(edit_);
  HFONT old = (HFONT)SelectObject(dc, font_.get());
  TEXTMETRICW tm = {};
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, old);
  ReleaseDC(edit_, dc);
  int h = tm.tmHeight + tm.tmExternalLeading;
  return h > 0 ? h : 16;
}

// 휠 입력 처리. settings.smoothScroll 가 꺼져 있으면 네이티브 줄 스크롤(즉시
// 점프), 켜져 있으면 픽셀 단위 애니메이션. 두 경우 모두 한 칸당 줄 수는
// settings.scrollLines 를 따른다.
void Editor::wheelScroll(int wheelDelta) {
  int lines = settings_->scrollLines;
  if (lines < 1) lines = 1;
  double notches = (double)wheelDelta / WHEEL_DELTA;

  if (!settings_->smoothScroll) {  // 즉시 줄 스크롤
    int dl = (int)std::lround(-notches * lines);  // 휠 위(양수) = 위로 스크롤
    if (dl != 0) SendMessageW(edit_, EM_LINESCROLL, 0, (LPARAM)dl);
    if (onScroll) onScroll();
    return;
  }

  // 부드러운 픽셀 애니메이션: 목표 위치에 누적(연속 휠이면 가속처럼 느껴짐).
  // 진행 중이 아니면 현재 위치에서 시작하고 타이머를 건다.
  int lh = lineHeightPx();
  double step = notches * (double)lines * lh;
  if (!smoothActive_) {
    POINT p = {};
    SendMessageW(edit_, EM_GETSCROLLPOS, 0, (LPARAM)&p);
    smoothCurY_ = p.y;
    smoothTargetY_ = p.y;
  }
  smoothTargetY_ -= step;  // 휠 위로(양수) = 내용 위로 = y 감소
  if (smoothTargetY_ < 0) smoothTargetY_ = 0;
  if (!smoothActive_) {
    smoothActive_ = true;
    SetTimer(edit_, IDT_SMOOTHSCROLL, 16, nullptr);  // 약 60fps
  }
}

// 현재 위치를 목표로 지수 이징(프레임당 약 28%) 이동. 목표 도달 또는 상/하단
// 한계로 더 못 가면 멈춘다.
void Editor::smoothScrollTick() {
  double dy = smoothTargetY_ - smoothCurY_;
  bool done = false;
  if (std::fabs(dy) < 0.5) {
    smoothCurY_ = smoothTargetY_;
    done = true;
  } else {
    smoothCurY_ += dy * 0.28;
  }
  POINT p = {0, (LONG)std::lround(smoothCurY_)};
  SendMessageW(edit_, EM_SETSCROLLPOS, 0, (LPARAM)&p);
  POINT got = {};
  SendMessageW(edit_, EM_GETSCROLLPOS, 0, (LPARAM)&got);
  if (got.y != p.y) {  // 상/하단에서 클램프됨: 더 못 감
    smoothCurY_ = smoothTargetY_ = got.y;
    done = true;
  }
  if (onScroll) onScroll();
  if (done) stopSmoothScroll();
}

void Editor::stopSmoothScroll() {
  if (!smoothActive_) return;
  KillTimer(edit_, IDT_SMOOTHSCROLL);
  smoothActive_ = false;
}

LRESULT Editor::onMessage(HWND e, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_KEYDOWN: {
      stopSmoothScroll();  // 키 입력 시 휠 관성 중단(캐럿 이동과 충돌 방지)
      bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      if (ctrl) {
        if (w == 'A') {  // 전체 선택
          SendMessageW(e, EM_SETSEL, 0, (LPARAM)-1);
          swallowChar_ = true;
          return 0;
        }
        if (w == 'Y') {  // 다시 실행(RichEdit 다단계 redo)
          SendMessageW(e, EM_REDO, 0, 0);
          swallowChar_ = true;
          return 0;
        }
        if (w == 'C' || w == 'X') {  // 선택 없으면 라인 복사/잘라내기
          DWORD a, b;
          editGetSel(e, a, b);
          if (a == b) {
            if (w == 'C')
              copyLine();
            else
              cutLine();
            swallowChar_ = true;
            return 0;
          }  // 선택이 있으면 네이티브 동작으로 통과
        }
        if (w == VK_DELETE) {  // 현재 라인 삭제(WM_CHAR 없음 -> swallow 안 함)
          deleteLine();
          return 0;
        }
      }
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
        if (autoIndentEnter()) {  // 들여쓰기 유지(+코드블록 중괄호 증가)
          swallowChar_ = true;
          return 0;
        }
      }
      if (w == VK_BACK) {  // 빈 짝(괄호/따옴표 등) 사이면 양쪽 삭제
        if (pairBackspace()) {
          swallowChar_ = true;  // 뒤따르는 WM_CHAR(0x08) 삼킴
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
    }
    case WM_CHAR:
      if (swallowChar_) {
        swallowChar_ = false;
        return 0;
      }
      if (autoPair((wchar_t)w)) return 0;  // 짝 처리 시 기본 입력 차단
      break;
    case WM_MOUSEWHEEL:
      // Ctrl+휠은 RichEdit 기본 동작(글꼴 줌)에 맡긴다.
      if (LOWORD(w) & MK_CONTROL) {
        LRESULT r = CallWindowProcW(orig_, e, m, w, l);
        if (onScroll) onScroll();
        return r;
      }
      wheelScroll((short)HIWORD(w));  // 부드러운 애니메이션 스크롤
      return 0;
    case WM_VSCROLL: {  // 스크롤바 직접 조작: 관성 중단 후 기본 처리
      stopSmoothScroll();
      LRESULT r = CallWindowProcW(orig_, e, m, w, l);
      if (onScroll) onScroll();
      return r;
    }
    case WM_LBUTTONDOWN:
      stopSmoothScroll();  // 클릭/드래그 시작 시 관성 중단
      break;
    case WM_TIMER:
      if (w == IDT_SMOOTHSCROLL) {
        smoothScrollTick();
        return 0;
      }
      break;
  }
  return CallWindowProcW(orig_, e, m, w, l);
}

LRESULT CALLBACK Editor::proc(HWND e, UINT m, WPARAM w, LPARAM l) {
  Editor* self = (Editor*)GetWindowLongPtrW(e, GWLP_USERDATA);
  if (!self) return DefWindowProcW(e, m, w, l);
  return self->onMessage(e, m, w, l);
}
