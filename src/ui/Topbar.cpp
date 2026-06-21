#include "ui/Topbar.h"

#include <algorithm>

#include "commands.h"
#include "core/dpi.h"
#include "core/raii.h"
#include "core/str_util.h"

static int textW(HDC dc, const std::wstring& s) {
  SIZE sz;
  GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
  return sz.cx;
}

void Topbar::layout(HWND hwnd, int width, HFONT uiFont, UINT dpi) {
  auto s = [dpi](int px) { return dpi::scale(px, dpi); };
  btns_.clear();
  HDC dc = GetDC(hwnd);
  HFONT old = (HFONT)SelectObject(dc, uiFont);
  int x = width;
  int barH = s(kTopbarH);
  const int wc = s(46);
  // Segoe MDL2 Assets 캡션 글리프: 최소화 E921, 최대화 E922(복원 E923), 닫기
  // E8BB
  btns_.push_back({IDM_WCLOSE, L"\xE8BB", {x - wc, 0, x, barH}, 2});
  x -= wc;
  btns_.push_back({IDM_MAX, L"\xE922", {x - wc, 0, x, barH}, 1});
  x -= wc;
  btns_.push_back({IDM_MIN, L"\xE921", {x - wc, 0, x, barH}, 1});
  x -= wc;
  x -= s(10);
  const int bh = s(26), bt = (barH - bh) / 2;
  // 보기 모드 세그먼트 (미리/분할/편집)
  TopBtn views[] = {
      {IDM_VIEW_P, W(u8"미리"), {}, 3},
      {IDM_VIEW_S, W(u8"분할"), {}, 3},
      {IDM_VIEW_E, W(u8"편집"), {}, 3},
  };
  for (TopBtn& v : views) {
    int w = textW(dc, v.label) + s(16);
    v.rc = {x - w, bt, x, bt + bh};
    btns_.push_back(v);
    x -= w + s(2);
  }
  x -= s(10);
  // 파일 버튼 (배열 앞 항목이 화면 오른쪽에 배치됨)
  TopBtn items[] = {
      {IDM_VSCODE, W(u8"VSCode"), {}, 0},
      {IDM_SAVE, W(u8"저장"), {}, 0},
      {IDM_OPEN, W(u8"열기"), {}, 0},
      {IDM_NEW, W(u8"새 파일"), {}, 0},
      {IDM_SETTINGS, W(u8"설정"), {}, 0},
      {IDM_FORMATTABLE, W(u8"정렬"), {}, 0},
      {IDM_INSERTTABLE, W(u8"표"), {}, 0},
  };
  for (TopBtn& it : items) {
    int w = textW(dc, it.label) + s(18);
    it.rc = {x - w, bt, x, bt + bh};
    btns_.push_back(it);
    x -= w + s(4);
  }
  SelectObject(dc, old);
  ReleaseDC(hwnd, dc);
}

int Topbar::btnAt(int x, int y) const {
  POINT p = {x, y};
  for (size_t i = 0; i < btns_.size(); i++)
    if (PtInRect(&btns_[i].rc, p)) return (int)i;
  return -1;
}
int Topbar::leftmostBtnX(int width) const {
  int m = width;
  for (const TopBtn& b : btns_) m = std::min(m, (int)b.rc.left);
  return m;
}

void Topbar::paint(HDC dc, int width, const Theme& theme, HFONT uiFont,
                   HFONT glyphFont, bool dirty, const std::wstring& name,
                   int view, bool maximized, UINT dpi) {
  auto s = [dpi](int px) { return dpi::scale(px, dpi); };
  int barH = s(kTopbarH);
  RECT bar = {0, 0, width, barH};
  BrushHandle bg(CreateSolidBrush(theme.topbarBg()));
  FillRect(dc, &bar, (HBRUSH)bg.get());
  PenHandle pen(CreatePen(PS_SOLID, 1, theme.border()));
  HPEN oldPen = (HPEN)SelectObject(dc, pen.get());
  MoveToEx(dc, 0, barH - 1, nullptr);
  LineTo(dc, width, barH - 1);
  SelectObject(dc, oldPen);

  HFONT oldFont = (HFONT)SelectObject(dc, uiFont);
  SetBkMode(dc, TRANSPARENT);

  int fx = s(12);
  if (dirty) {
    SetTextColor(dc, theme.accent());
    RECT dr = {fx, 0, fx + s(14), barH};
    DrawTextW(dc, L"\x25CF", 1, &dr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    fx += s(16);
  }
  SetTextColor(dc, theme.fg());
  RECT nr = {fx, 0, leftmostBtnX(width) - s(8), barH};
  DrawTextW(dc, name.c_str(), -1, &nr,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  for (size_t i = 0; i < btns_.size(); i++) {
    TopBtn& b = btns_[i];
    bool hot = ((int)i == hot_);
    bool active = false;
    if (b.type == 3) {
      int bv = (b.id == IDM_VIEW_E) ? 0 : (b.id == IDM_VIEW_S) ? 1 : 2;
      active = (bv == view);
    }
    if (active) {
      BrushHandle hb(CreateSolidBrush(theme.accent()));
      FillRect(dc, &b.rc, (HBRUSH)hb.get());
    } else if (hot) {
      COLORREF hc = (b.type == 2) ? RGB(0xe8, 0x11, 0x23) : theme.btnHover();
      BrushHandle hb(CreateSolidBrush(hc));
      FillRect(dc, &b.rc, (HBRUSH)hb.get());
    }
    COLORREF tc;
    if (active)
      tc = RGB(0xff, 0xff, 0xff);
    else if (b.type == 0)
      tc = theme.fg();
    else
      tc = hot ? (b.type == 2 ? RGB(0xff, 0xff, 0xff) : theme.fg())
               : theme.muted();
    SetTextColor(dc, tc);
    const wchar_t* glyph = b.label.c_str();
    if (b.id == IDM_MAX && maximized)
      glyph = L"\xE923";  // 최대화 상태면 복원 아이콘
    SelectObject(dc, (b.type == 1 || b.type == 2) ? glyphFont : uiFont);
    DrawTextW(dc, glyph, -1, &b.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  SelectObject(dc, oldFont);
}
