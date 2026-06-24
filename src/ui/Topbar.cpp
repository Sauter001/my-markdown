#include "ui/Topbar.h"

#include <algorithm>

#include "commands.h"
#include "core/dpi.h"
#include "core/raii.h"
#include "core/str_util.h"

// VS Code 로고 외곽 리본(공식 SVG 좌표를 0..100 으로 정규화한 꼭짓점).
static const double kVsOuter[][2] = {
    {75.87, 99.13}, {96.46, 89.21}, {100, 83.58}, {100, 16.42}, {96.46, 10.79},
    {75.87, 0.87},  {68.77, 2.07},  {29.34, 38.09}, {12.17, 25.05}, {6.84, 25.29},
    {1.33, 30.31},  {1.32, 36.46},  {16.24, 50.0},  {1.32, 63.54},  {1.33, 69.69},
    {6.84, 74.71},  {12.17, 74.95}, {29.34, 61.91}, {68.77, 97.93}};
// 가운데 접힘(밝은 면) 삼각형.
static const double kVsFold[][2] = {{75.02, 27.30}, {45.11, 50.0}, {75.02, 72.70}};

// VS Code 로고를 버튼 사각형 가운데에 그린다. 작은 크기 계단현상을 줄이기 위해
// 4배 크기로 그린 뒤 HALFTONE 으로 축소한다(배경 bg 로 채워 안티에일리어싱 효과).
static void drawVSCodeLogo(HDC dc, const RECT& rc, COLORREF bg) {
  int bh = rc.bottom - rc.top;
  int L = (int)(bh * 0.66);
  if (L < 8) L = 8;
  int ox = (rc.left + rc.right) / 2 - L / 2;
  int oy = (rc.top + rc.bottom) / 2 - L / 2;
  const int SS = 4;
  int N = L * SS;
  HDC mem = CreateCompatibleDC(dc);
  HBITMAP bmp = CreateCompatibleBitmap(dc, N, N);
  HBITMAP oldbmp = (HBITMAP)SelectObject(mem, bmp);
  RECT full = {0, 0, N, N};
  BrushHandle bgbr(CreateSolidBrush(bg));
  FillRect(mem, &full, (HBRUSH)bgbr.get());
  POINT outer[19], fold[3];
  for (int i = 0; i < 19; i++) {
    outer[i].x = (LONG)(kVsOuter[i][0] / 100.0 * N + 0.5);
    outer[i].y = (LONG)(kVsOuter[i][1] / 100.0 * N + 0.5);
  }
  for (int i = 0; i < 3; i++) {
    fold[i].x = (LONG)(kVsFold[i][0] / 100.0 * N + 0.5);
    fold[i].y = (LONG)(kVsFold[i][1] / 100.0 * N + 0.5);
  }
  BrushHandle mainbr(CreateSolidBrush(RGB(0x24, 0x96, 0xD8)));   // 리본
  BrushHandle foldbr(CreateSolidBrush(RGB(0x35, 0xA6, 0xEC)));   // 접힘(밝은 면)
  HGDIOBJ oP = SelectObject(mem, GetStockObject(NULL_PEN));
  HGDIOBJ oB = SelectObject(mem, (HBRUSH)mainbr.get());
  Polygon(mem, outer, 19);
  SelectObject(mem, (HBRUSH)foldbr.get());
  Polygon(mem, fold, 3);
  SelectObject(mem, oB);
  SelectObject(mem, oP);
  int oldMode = SetStretchBltMode(dc, HALFTONE);
  SetBrushOrgEx(dc, 0, 0, nullptr);
  StretchBlt(dc, ox, oy, L, L, mem, 0, 0, N, N, SRCCOPY);
  SetStretchBltMode(dc, oldMode);
  SelectObject(mem, oldbmp);
  DeleteObject(bmp);
  DeleteDC(mem);
}

// IDM 으로 commandTable() 메타(글리프/툴팁/종류)를 채워 TopBtn 을 만든다.
// 위치(rc)는 호출부에서 설정한다.
static TopBtn makeBtn(int idm) {
  TopBtn b{};
  b.id = idm;
  if (const CommandInfo* ci = findCommand(idm)) {
    b.label = ci->glyph ? ci->glyph : L"";
    b.tip = ci->tip ? W(ci->tip) : L"";
    b.type = ci->btn;
  }
  return b;
}

void Topbar::layout(int width, UINT dpi) {
  auto s = [dpi](int px) { return dpi::scale(px, dpi); };
  btns_.clear();
  int x = width;
  int barH = s(kTopbarH);
  const int wc = s(46);
  auto push = [&](int idm, RECT rc) {
    TopBtn b = makeBtn(idm);
    b.rc = rc;
    btns_.push_back(b);
  };
  // 창 제어(닫기/최대화/최소화) - 글리프는 commandTable() 에서.
  push(IDM_WCLOSE, {x - wc, 0, x, barH});
  x -= wc;
  push(IDM_MAX, {x - wc, 0, x, barH});
  x -= wc;
  push(IDM_MIN, {x - wc, 0, x, barH});
  x -= wc;
  x -= s(10);
  const int bh = s(26), bt = (barH - bh) / 2;
  const int iw = s(34);  // 아이콘 버튼 고정 폭
  // 보기 모드 세그먼트 (미리/분할/편집)
  const int views[] = {IDM_VIEW_P, IDM_VIEW_S, IDM_VIEW_E};
  for (int idm : views) {
    push(idm, {x - iw, bt, x, bt + bh});
    x -= iw + s(2);
  }
  x -= s(10);
  // 파일/기능 버튼 (배열 앞 항목이 화면 오른쪽에 배치됨 -> 화면 좌->우 순서는
  // 배열 역순). 좌->우: 새파일, 열기, 저장, 목차삽입, 표삽입, 표정렬, 설정, VS Code
  const int items[] = {IDM_VSCODE,     IDM_SETTINGS,    IDM_FORMATTABLE,
                       IDM_INSERTTABLE, IDM_INSERTTOC,  IDM_SAVE,
                       IDM_OPEN,       IDM_NEW};
  for (int idm : items) {
    push(idm, {x - iw, bt, x, bt + bh});
    x -= iw + s(4);
  }
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
    if (b.type == BtnType::View) {
      int bv = (b.id == IDM_VIEW_E) ? 0 : (b.id == IDM_VIEW_S) ? 1 : 2;
      active = (bv == view);
    }
    if (active) {
      BrushHandle hb(CreateSolidBrush(theme.accent()));
      FillRect(dc, &b.rc, (HBRUSH)hb.get());
    } else if (hot) {
      COLORREF hc = (b.type == BtnType::Close) ? RGB(0xe8, 0x11, 0x23)
                                               : theme.btnHover();
      BrushHandle hb(CreateSolidBrush(hc));
      FillRect(dc, &b.rc, (HBRUSH)hb.get());
    }
    if (b.id == IDM_VSCODE) {  // 글리프 대신 VS Code 로고
      drawVSCodeLogo(dc, b.rc, hot ? theme.btnHover() : theme.topbarBg());
      continue;
    }
    COLORREF tc;
    if (active)
      tc = RGB(0xff, 0xff, 0xff);
    else if (b.type == BtnType::File)
      tc = theme.fg();
    else
      tc = hot ? (b.type == BtnType::Close ? RGB(0xff, 0xff, 0xff) : theme.fg())
               : theme.muted();
    SetTextColor(dc, tc);
    const wchar_t* glyph = b.label.c_str();
    if (b.id == IDM_MAX && maximized)
      glyph = L"\xE923";  // 최대화 상태면 복원 아이콘
    SelectObject(dc, glyphFont);  // 모든 버튼이 아이콘(글리프)
    DrawTextW(dc, glyph, -1, &b.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  SelectObject(dc, oldFont);
}
