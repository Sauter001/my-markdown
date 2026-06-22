#include "App.h"

#include <windowsx.h>

#include <cstring>
#include <unordered_map>
#include <vector>

#include "commands.h"
#include "core/dpi.h"
#include "core/file_io.h"
#include "core/str_util.h"
#include "model/Keymap.h"
#include "resource.h"

static const int kResizeBorder = 6;
static const int kZoomStep = 10;  // 줌 단계(%)
static const int kDividerW = 5;

// ---------------------------------------------------------------------------
// 콜백 배선 (컴포넌트 간 통지)
// ---------------------------------------------------------------------------
void App::wireCallbacks() {
  editor_.onTableNav = [this](bool s) {
    return tableEditor_.nav(editor_.hwnd(), s);
  };
  editor_.onTableEnter = [this]() {
    return tableEditor_.enter(editor_.hwnd());
  };
  editor_.onScroll = [this]() { requestScrollSync(); };
  preview_.onReady = [this]() { pushPreviewConfigAndRender(); };
  preview_.onOpenExternal = [this](const std::string& req) {
    return openExternal(req);
  };
  // 미리보기(WebView2) 포커스에서는 호스트 ACCEL 이 키를 못 받으므로,
  // preview.js 가 가로채 보낸 단축키 id 를 명령으로 변환해 WM_COMMAND 로
  // 큐잉한다(재진입 안전). id 목록은 web/preview.js 의 키 매핑과 동기화한다.
  preview_.onAccel = [this](const std::string& id) {
    static const std::unordered_map<std::string, int> kMap = {
        {"new", IDM_NEW},
        {"open", IDM_OPEN},
        {"save", IDM_SAVE},
        {"saveAs", IDM_SAVEAS},
        {"openInVSCode", IDM_VSCODE},
        {"settings", IDM_SETTINGS},
        {"zoomIn", IDM_ZOOM_IN},
        {"zoomOut", IDM_ZOOM_OUT},
        {"zoomReset", IDM_ZOOM_RESET},
        {"viewEditor", IDM_VIEW_E},
        {"viewSplit", IDM_VIEW_S},
        {"viewPreview", IDM_VIEW_P},
        {"cycleView", IDM_CYCLE},
    };
    auto it = kMap.find(id);
    if (it != kMap.end())
      PostMessageW(hwnd_, WM_COMMAND, MAKEWPARAM(it->second, 0), 0);
  };
}

// ---------------------------------------------------------------------------
// 문서/제목
// ---------------------------------------------------------------------------
void App::setCurrentFile(const std::wstring& path) {
  curPath_ = path;
  curName_ = baseName(path);
  curDir_ = dirOf(path);
}
void App::invalidateTopbar() {
  if (!hwnd_) return;
  RECT cr;
  GetClientRect(hwnd_, &cr);
  RECT bar = {0, 0, cr.right, dpiScale(kTopbarH)};
  InvalidateRect(hwnd_, &bar, FALSE);
}
void App::updateTitle() {
  std::wstring t;
  if (dirty_) t += L"* ";
  t += curName_.empty() ? W(u8"제목 없음") : curName_;
  if (hwnd_) SetWindowTextW(hwnd_, t.c_str());
  invalidateTopbar();
}

// ---------------------------------------------------------------------------
// 파일 대화상자 + 동작
// ---------------------------------------------------------------------------
bool App::openDialog(std::wstring& outPath) {
  wchar_t buf[4096] = {0};
  OPENFILENAMEW ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd_;
  ofn.lpstrFilter =
      L"Markdown (*.md;*.markdown;*.txt)\0*.md;*.markdown;*.mdown;*.txt\0"
      L"All files (*.*)\0*.*\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = 4096;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetOpenFileNameW(&ofn)) {
    outPath = buf;
    return true;
  }
  return false;
}
bool App::saveDialog(const std::wstring& suggested, std::wstring& outPath) {
  wchar_t buf[4096] = {0};
  if (!suggested.empty()) wcsncpy(buf, suggested.c_str(), 4095);
  OPENFILENAMEW ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd_;
  ofn.lpstrFilter = L"Markdown (*.md)\0*.md\0All files (*.*)\0*.*\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = 4096;
  ofn.lpstrDefExt = L"md";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetSaveFileNameW(&ofn)) {
    outPath = buf;
    return true;
  }
  return false;
}
bool App::confirmDiscard() {
  if (!dirty_) return true;
  int r =
      MessageBoxW(hwnd_,
                  W("\xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x98\xEC\xA7\x80 "
                    "\xEC\x95\x8A\xEC\x9D\x80 "
                    "\xEB\xB3\x80\xEA\xB2\xBD\xEC\x82\xAC\xED\x95\xAD\xEC\x9D"
                    "\xB4 \xEC\x9E\x88\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4. "
                    "\xEA\xB3\x84\xEC\x86\x8D\xED\x95\x98\xEC\x8B\x9C\xEA\xB2"
                    "\xA0\xEC\x8A\xB5\xEB\x8B\x88\xEA\xB9\x8C?")
                      .c_str(),
                  L"MyMD", MB_YESNO | MB_ICONWARNING);
  return r == IDYES;
}
// 현재 본문을 "저장됨" 기준선으로 기록하고 더티를 해제한다. 이후 편집이
// 이 기준선과 같아지면(되돌림) 더티가 자동 해제된다(IDT_RENDER 디바운스에서 비교).
void App::markSaved() {
  savedContent_ = editor_.getTextUtf8Lf();  // getTextUtf8Lf 와 동일 정규형으로 보관
  dirty_ = false;
}
bool App::saveFileAs() {
  std::wstring suggested = curName_.empty() ? L"untitled.md" : curName_;
  std::wstring path;
  if (!saveDialog(suggested, path)) return false;
  if (!writeFile(path, editor_.getTextUtf8Lf())) {
    MessageBoxW(hwnd_, L"write failed", L"MyMD", MB_OK | MB_ICONERROR);
    return false;
  }
  setCurrentFile(path);
  markSaved();
  updateTitle();
  refreshPreview();  // 경로(이미지 base) 변경 반영
  return true;
}
bool App::saveFile() {
  if (curPath_.empty()) return saveFileAs();
  if (!writeFile(curPath_, editor_.getTextUtf8Lf())) {
    MessageBoxW(hwnd_, L"write failed", L"MyMD", MB_OK | MB_ICONERROR);
    return false;
  }
  markSaved();
  updateTitle();
  return true;
}
void App::newFile() {
  if (!confirmDiscard()) return;
  curPath_.clear();
  curName_.clear();
  curDir_.clear();
  editor_.setTextUtf8Lf("");
  markSaved();
  updateTitle();
  refreshPreview();
  SetFocus(editor_.hwnd());
}
void App::openFile() {
  if (!confirmDiscard()) return;
  std::wstring path;
  if (!openDialog(path)) return;
  std::string content;
  if (!readFile(path, content)) {
    MessageBoxW(hwnd_, L"read failed", L"MyMD", MB_OK | MB_ICONERROR);
    return;
  }
  setCurrentFile(path);
  editor_.setTextUtf8Lf(content);
  markSaved();
  updateTitle();
  refreshPreview();
  SetFocus(editor_.hwnd());
}

// 설치된 Code.exe를 우선 찾아 파일 인자로 실행하고,
// 못 찾으면 PATH의 code(code.cmd)로 창 없이 폴백한다.
bool App::launchVSCode(const std::wstring& file) {
  auto envp = [](const wchar_t* name) -> std::wstring {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : std::wstring();
  };
  auto tryExe = [&](const std::wstring& exe) -> bool {
    if (exe.empty()) return false;
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES)
      return false;
    std::wstring params = L"\"" + file + L"\"";
    return (INT_PTR)ShellExecuteW(hwnd_, L"open", exe.c_str(), params.c_str(),
                                  nullptr, SW_SHOWNORMAL) > 32;
  };
  std::wstring local = envp(L"LOCALAPPDATA");
  std::wstring pf = envp(L"ProgramFiles");
  std::wstring pf86 = envp(L"ProgramFiles(x86)");
  if (!local.empty() &&
      tryExe(local + L"\\Programs\\Microsoft VS Code\\Code.exe"))
    return true;
  if (!pf.empty() && tryExe(pf + L"\\Microsoft VS Code\\Code.exe")) return true;
  if (!pf86.empty() && tryExe(pf86 + L"\\Microsoft VS Code\\Code.exe"))
    return true;
  // 폴백: PATH의 code(code.cmd)를 콘솔 창 없이 실행
  std::wstring cmd = L"cmd.exe /c code \"" + file + L"\"";
  std::vector<wchar_t> cbuf(cmd.begin(), cmd.end());
  cbuf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(nullptr, cbuf.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    UniqueHandle thread(pi.hThread), proc(pi.hProcess);  // 핸들 자동 닫기
    return true;
  }
  return false;
}
void App::openInVSCode() {
  if (dirty_ || curPath_.empty()) {
    if (!saveFile()) return;  // 저장 취소/실패 시 중단
  }
  if (curPath_.empty()) return;
  if (!launchVSCode(curPath_)) {
    MessageBoxW(hwnd_,
                W(u8"VSCode(Code.exe)를 찾을 수 없습니다. 설치 여부나 PATH를 "
                  u8"확인하세요.")
                    .c_str(),
                L"MyMD", MB_OK | MB_ICONERROR);
  }
}

// 미리보기 외부 링크를 기본 브라우저로 (http/https/mailto 만 허용)
std::string App::openExternal(const std::string& req) {
  std::string url = base64_decode(firstStringArg(req));
  auto startsWith = [&](const char* p) {
    size_t n = std::strlen(p);
    return url.size() >= n && _strnicmp(url.c_str(), p, (int)n) == 0;
  };
  if (!startsWith("http://") && !startsWith("https://") &&
      !startsWith("mailto:"))
    return "{\"ok\":false}";
  ShellExecuteW(hwnd_, L"open", utf8_to_wide(url).c_str(), nullptr, nullptr,
                SW_SHOWNORMAL);
  return "{\"ok\":true}";
}

// ---------------------------------------------------------------------------
// 프리뷰 + 보기 레이아웃
// ---------------------------------------------------------------------------
void App::pushPreviewConfigAndRender() {
  preview_.pushConfig(theme_.isDark(), settings_.langsJson, curDir_,
                      settings_.zoom);
  preview_.pushKeymap(keymap::previewJson(
      settings_.keymapJson));  // 미리보기 포커스 단축키 동기화
  preview_.pushRender(editor_.getTextUtf8Lf());
}
void App::refreshPreview() {
  if (view_ == 0) return;
  pushPreviewConfigAndRender();
}
void App::layout() {
  if (!editor_.hwnd()) return;
  UINT nd =
      dpi::forWindow(hwnd_);  // DPI 변경(모니터 이동 등) 감지 시 폰트 재생성
  if (nd != dpi_) {
    dpi_ = nd;
    makeUiFonts();
    editor_.applyStyle();
  }
  RECT cr;
  GetClientRect(hwnd_, &cr);
  int Wd = cr.right, H = cr.bottom;
  int barH = dpiScale(kTopbarH), divW = dpiScale(kDividerW);
  int top = barH, ch = H - barH;
  if (ch < 0) ch = 0;
  topbar_.layout(hwnd_, Wd, uiFont_.get(), dpi_);
  dividerX_ = -1;
  HWND edit = editor_.hwnd(), prev = preview_.host();
  if (view_ == 0) {  // 에디터만
    MoveWindow(edit, 0, top, Wd, ch, TRUE);
    ShowWindow(edit, SW_SHOW);
    if (prev) ShowWindow(prev, SW_HIDE);
  } else if (view_ == 2) {  // 미리보기만
    ShowWindow(edit, SW_HIDE);
    if (prev) {
      MoveWindow(prev, 0, top, Wd, ch, TRUE);
      ShowWindow(prev, SW_SHOW);
    }
  } else {  // 분할
    int ew = (int)(Wd * splitRatio_), minw = dpiScale(120);
    if (ew < minw) ew = minw;
    if (ew > Wd - minw - divW) ew = Wd - minw - divW;
    if (ew < 0) ew = 0;
    MoveWindow(edit, 0, top, ew, ch, TRUE);
    ShowWindow(edit, SW_SHOW);
    int px = ew + divW;
    if (prev) {
      MoveWindow(prev, px, top, Wd - px, ch, TRUE);
      ShowWindow(prev, SW_SHOW);
    }
    dividerX_ = ew;
  }
  preview_.updateBounds();
  InvalidateRect(hwnd_, nullptr, FALSE);
}
void App::setView(int v) {
  view_ = v;
  if (v != 0)
    preview_.ensureEngine(
        editor_.hwnd());  // 에디터 전용이 아니면 엔진 지연 생성
  layout();
  refreshPreview();
  SetFocus(editor_.hwnd());
}

// 스크롤 동기화 (에디터 -> 프리뷰). 분할 보기 + settings.scrollSync 일 때만.
void App::requestScrollSync() {
  if (!settings_.scrollSync || view_ != 1) return;
  SetTimer(hwnd_, IDT_SCROLLSYNC, 16, nullptr);  // 연속 이벤트 합치기(디바운스)
}
void App::syncPreviewScroll() {
  if (!settings_.scrollSync || view_ != 1 || !preview_.ready()) return;
  SCROLLINFO si = {sizeof(si)};
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  if (!GetScrollInfo(editor_.hwnd(), SB_VERT, &si)) return;
  int denom =
      si.nMax - si.nMin - (int)si.nPage + 1;  // 최대 스크롤 위치(라인 단위)
  double ratio = denom > 0 ? (double)(si.nPos - si.nMin) / (double)denom : 0.0;
  if (ratio < 0.0) ratio = 0.0;
  if (ratio > 1.0) ratio = 1.0;
  preview_.scrollTo(ratio);
}

// ---------------------------------------------------------------------------
// 줌 + 설정 적용
// ---------------------------------------------------------------------------
void App::setZoom(int z) {
  if (z < 50) z = 50;
  if (z > 300) z = 300;
  if (z == settings_.zoom) return;
  settings_.zoom = z;
  editor_.applyStyle();
  preview_.pushZoom(settings_.zoom);
  settings_.save();
}
int App::dpiScale(int px96) const { return dpi::scale(px96, dpi_); }

void App::makeUiFonts() {
  uiFont_ = FontHandle(
      CreateFontW(-dpiScale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                  CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI"));
  glyphFont_ = FontHandle(CreateFontW(
      -dpiScale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets"));
}

// 설정 대화상자 저장 후, 변경된 항목만 런타임에 반영한다(전후 스냅샷 비교).
void App::applySettingsChange(const Settings& before) {
  editor_.applyStyle();  // 글꼴/탭폭 즉시
  if (settings_.wrap != before.wrap) {
    editor_.recreateForWrap(hwnd_);
    layout();
    SetFocus(editor_.hwnd());
  }
  if (settings_.theme != before.theme) {  // 테마: 에디터색 + 창 브러시 갱신
    theme_.setMode(settings_.theme);
    editor_.applyColors(theme_.bg(), theme_.fg());  // RichEdit 배경/글자색
    editBrush_.reset(CreateSolidBrush(theme_.bg()));  // 창 클래스 배경
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
  if (settings_.keymapJson != before.keymapJson) {  // 단축키 재바인딩
    rebuildAccel();                                 // 에디터 포커스 ACCEL
    if (preview_.ready())
      preview_.pushKeymap(
          keymap::previewJson(settings_.keymapJson));  // 미리보기 포커스
  }
  if (settings_.theme != before.theme ||
      settings_.langsJson != before.langsJson)
    refreshPreview();
  settings_.save();
}

// ---------------------------------------------------------------------------
// 명령 디스패치
// ---------------------------------------------------------------------------
void App::runBtn(int id) {
  switch (id) {
    case IDM_NEW:
      newFile();
      break;
    case IDM_OPEN:
      openFile();
      break;
    case IDM_SAVE:
      saveFile();
      break;
    case IDM_SAVEAS:
      saveFileAs();
      break;
    case IDM_VSCODE:
      openInVSCode();
      break;
    case IDM_INSERTTABLE: {
      int c, r;
      if (tableDialog_.show(hwnd_, uiFont_.get(), c, r))
        tableEditor_.insertSkeleton(editor_.hwnd(), c, r);
      SetFocus(editor_.hwnd());
      break;
    }
    case IDM_FORMATTABLE:
      SetFocus(editor_.hwnd());
      tableEditor_.formatTable(editor_.hwnd());
      break;
    case IDM_SETTINGS: {
      Settings before = settings_;
      if (settingsDialog_.show(hwnd_, uiFont_.get(), settings_))
        applySettingsChange(before);
      SetFocus(editor_.hwnd());
      break;
    }
    case IDM_ZOOM_IN:
      setZoom(settings_.zoom + kZoomStep);
      break;
    case IDM_ZOOM_OUT:
      setZoom(settings_.zoom - kZoomStep);
      break;
    case IDM_ZOOM_RESET:
      setZoom(100);
      break;
    case IDM_MIN:
      ShowWindow(hwnd_, SW_MINIMIZE);
      break;
    case IDM_MAX:
      ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
      break;
    case IDM_WCLOSE:
      SendMessageW(hwnd_, WM_CLOSE, 0, 0);
      break;
    case IDM_VIEW_E:
      setView(0);
      break;
    case IDM_VIEW_S:
      setView(1);
      break;
    case IDM_VIEW_P:
      setView(2);
      break;
    case IDM_CYCLE:
      setView((view_ + 1) % 3);
      break;  // 편집 -> 분할 -> 미리보기 순환
  }
}

// ---------------------------------------------------------------------------
// 메인 창 프로시저
// ---------------------------------------------------------------------------
LRESULT App::onMessage(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      editor_.create(h, settings_);
      editor_.applyColors(theme_.bg(), theme_.fg());  // RichEdit 초기 테마색
      preview_.createHost(h);  // WebView2 는 지연 임베드, 초기엔 숨김
      return 0;
    }
    case WM_NCCALCSIZE:
      if (w == TRUE) {
        if (IsZoomed(h)) {  // 최대화 시 작업표시줄을 덮지 않게 프레임만큼 보정
          int fx = GetSystemMetrics(SM_CXSIZEFRAME) +
                   GetSystemMetrics(SM_CXPADDEDBORDER);
          int fy = GetSystemMetrics(SM_CYSIZEFRAME) +
                   GetSystemMetrics(SM_CXPADDEDBORDER);
          NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*)l;
          p->rgrc[0].left += fx;
          p->rgrc[0].right -= fx;
          p->rgrc[0].top += fy;
          p->rgrc[0].bottom -= fy;
        }
        return 0;  // 비클라이언트 제거 -> 전체가 클라이언트(프레임리스)
      }
      break;
    case WM_NCHITTEST: {
      RECT rc;
      GetWindowRect(h, &rc);
      int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
      if (!IsZoomed(h)) {
        int rb = dpiScale(kResizeBorder);
        bool L_ = x < rc.left + rb, R_ = x >= rc.right - rb;
        bool T_ = y < rc.top + rb, B_ = y >= rc.bottom - rb;
        if (T_ && L_) return HTTOPLEFT;
        if (T_ && R_) return HTTOPRIGHT;
        if (B_ && L_) return HTBOTTOMLEFT;
        if (B_ && R_) return HTBOTTOMRIGHT;
        if (L_) return HTLEFT;
        if (R_) return HTRIGHT;
        if (T_) return HTTOP;
        if (B_) return HTBOTTOM;
      }
      POINT cp = {x, y};
      ScreenToClient(h, &cp);
      if (cp.y < dpiScale(kTopbarH))
        return topbar_.btnAt(cp.x, cp.y) >= 0 ? HTCLIENT : HTCAPTION;
      return HTCLIENT;
    }
    case WM_ERASEBKGND:
      return 1;  // 깜빡임 방지: 상단바는 WM_PAINT, 나머지는 EDIT 가 그림
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      RECT cr;
      GetClientRect(h, &cr);
      std::wstring name = curName_.empty() ? W(u8"제목 없음") : curName_;
      topbar_.paint(dc, cr.right, theme_, uiFont_.get(), glyphFont_.get(),
                    dirty_, name, view_, IsZoomed(h) != 0, dpi_);
      if (view_ == 1 && dividerX_ >= 0) {  // 디바이더 스트립
        RECT dv = {dividerX_, dpiScale(kTopbarH),
                   dividerX_ + dpiScale(kDividerW), cr.bottom};
        BrushHandle b(CreateSolidBrush(theme_.border()));
        FillRect(dc, &dv, (HBRUSH)b.get());
      }
      EndPaint(h, &ps);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      int mx = GET_X_LPARAM(l), my = GET_Y_LPARAM(l);
      if (view_ == 1 && dividerX_ >= 0 && mx >= dividerX_ &&
          mx < dividerX_ + dpiScale(kDividerW) && my >= dpiScale(kTopbarH)) {
        SetCapture(h);
        divDrag_ = true;
        return 0;
      }
      int i = topbar_.btnAt(mx, my);
      if (i >= 0) runBtn(topbar_.btnId(i));
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (divDrag_) {
        RECT cr;
        GetClientRect(h, &cr);
        double r =
            cr.right > 0 ? (double)GET_X_LPARAM(l) / (double)cr.right : 0.5;
        if (r < 0.12) r = 0.12;
        if (r > 0.88) r = 0.88;
        splitRatio_ = r;
        layout();
        return 0;
      }
      int i = topbar_.btnAt(GET_X_LPARAM(l), GET_Y_LPARAM(l));
      if (i != topbar_.hot()) {
        topbar_.setHot(i);
        invalidateTopbar();
        TRACKMOUSEEVENT tme = {sizeof(tme)};
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = h;
        TrackMouseEvent(&tme);
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      if (topbar_.hot() != -1) {
        topbar_.setHot(-1);
        invalidateTopbar();
      }
      return 0;
    case WM_LBUTTONUP:
      if (divDrag_) {
        divDrag_ = false;
        ReleaseCapture();
      }
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(l) == HTCLIENT && view_ == 1 && dividerX_ >= 0) {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(h, &p);
        if (p.x >= dividerX_ && p.x < dividerX_ + dpiScale(kDividerW) &&
            p.y >= dpiScale(kTopbarH)) {
          SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
          return TRUE;
        }
      }
      break;
    case WM_APP_PREWARM:
      // 첫 페인트 직후 1회: 엔진을 미리 만들어 둔다. 준비되면 onReady 가
      // 초기 설정/렌더를 밀어넣으므로 여기서는 생성만 한다.
      preview_.ensureEngine(editor_.hwnd());
      return 0;
    case WM_TIMER:
      if (w == IDT_RENDER) {
        KillTimer(h, IDT_RENDER);
        std::string cur = editor_.getTextUtf8Lf();  // 본문 1회 추출(렌더/더티 공유)
        bool nd = (cur != savedContent_);  // 저장 상태와 동일하면 더티 해제(되돌림)
        if (nd != dirty_) {
          dirty_ = nd;
          updateTitle();
        }
        if (view_ != 0) preview_.pushRender(cur);
      } else if (w == IDT_SCROLLSYNC) {
        KillTimer(h, IDT_SCROLLSYNC);
        syncPreviewScroll();
      }
      return 0;
    case WM_SIZE:
      layout();
      return 0;
    case WM_DPICHANGED: {
      // 모니터 간 이동 등으로 DPI 변경: OS 권장 위치/크기를 적용한다.
      // 이어지는 WM_SIZE -> layout() 가 dpi_ 갱신 + 폰트 재생성을 처리.
      RECT* r = (RECT*)l;
      SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left,
                   r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_SETFOCUS:
      if (editor_.hwnd()) SetFocus(editor_.hwnd());
      return 0;
    case WM_CTLCOLOREDIT: {
      HDC dc = (HDC)w;
      SetTextColor(dc, theme_.fg());
      SetBkColor(dc, theme_.bg());
      return (LRESULT)editBrush_.get();
    }
    case WM_COMMAND: {
      if ((HWND)l == editor_.hwnd() && HIWORD(w) == EN_CHANGE) {
        if (!editor_.isSuppressing()) {
          if (!dirty_) {  // 즉시 더티 표시(반응성). 되돌림 감지는 디바운스에서.
            dirty_ = true;
            updateTitle();
          }
          // 디바운스: 더티 재평가(저장 상태와 동일하면 해제) + 프리뷰 렌더.
          // 편집 전용 보기에서도 되돌림 감지를 위해 항상 건다.
          SetTimer(h, IDT_RENDER, 120, nullptr);
        }
        return 0;
      }
      switch (LOWORD(w)) {
        case IDM_NEW:
          runBtn(IDM_NEW);
          return 0;
        case IDM_OPEN:
          runBtn(IDM_OPEN);
          return 0;
        case IDM_SAVE:
          runBtn(IDM_SAVE);
          return 0;
        case IDM_SAVEAS:
          runBtn(IDM_SAVEAS);
          return 0;
        case IDM_VSCODE:
          runBtn(IDM_VSCODE);
          return 0;
        case IDM_INSERTTABLE:
          runBtn(IDM_INSERTTABLE);
          return 0;
        case IDM_FORMATTABLE:
          runBtn(IDM_FORMATTABLE);
          return 0;
        case IDM_SETTINGS:
          runBtn(IDM_SETTINGS);
          return 0;
        case IDM_ZOOM_IN:
          runBtn(IDM_ZOOM_IN);
          return 0;
        case IDM_ZOOM_OUT:
          runBtn(IDM_ZOOM_OUT);
          return 0;
        case IDM_ZOOM_RESET:
          runBtn(IDM_ZOOM_RESET);
          return 0;
        case IDM_VIEW_E:
          runBtn(IDM_VIEW_E);
          return 0;
        case IDM_VIEW_S:
          runBtn(IDM_VIEW_S);
          return 0;
        case IDM_VIEW_P:
          runBtn(IDM_VIEW_P);
          return 0;
        case IDM_CYCLE:
          runBtn(IDM_CYCLE);
          return 0;
      }
      break;
    }
    case WM_GETMINMAXINFO: {
      MINMAXINFO* mmi = (MINMAXINFO*)l;
      mmi->ptMinTrackSize.x = dpiScale(480);
      mmi->ptMinTrackSize.y = dpiScale(320);
      return 0;
    }
    case WM_CLOSE:
      if (dirty_) {
        int r =
            MessageBoxW(h,
                        W("\xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x98\xEC\xA7\x80 "
                          "\xEC\x95\x8A\xEC\x9D\x80 \xEB\xB3\x80\xEA\xB2\xBD "
                          "\xEC\x82\xAC\xED\x95\xAD\xEC\x9D\xB4 "
                          "\xEC\x9E\x88\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4. "
                          "\xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x98\xEC\x8B\x9C\xEA"
                          "\xB2\xA0\xEC\x8A\xB5\xEB\x8B\x88\xEA\xB9\x8C?")
                            .c_str(),
                        L"MyMD", MB_YESNOCANCEL | MB_ICONWARNING);
        if (r == IDCANCEL) return 0;
        if (r == IDYES && !saveFile()) return 0;  // 저장 실패/취소 시 닫지 않음
      }
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      preview_
          .destroyEngine();  // 호스트 창이 살아 있을 때 WebView2 컨트롤러 정리
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

// 정적 thunk: GWLP_USERDATA 의 인스턴스로 위임 (단일 인스턴스 Win32 OOP 래핑)
LRESULT CALLBACK App::MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) {
    auto cs = (CREATESTRUCTW*)l;
    App* self = (App*)cs->lpCreateParams;
    self->hwnd_ = h;
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
  }
  App* app = (App*)GetWindowLongPtrW(h, GWLP_USERDATA);
  if (!app) return DefWindowProcW(h, m, w, l);
  return app->onMessage(h, m, w, l);
}

// ---------------------------------------------------------------------------
// 진입 (인자 파싱, 리소스, 창 생성, 메시지 루프)
// ---------------------------------------------------------------------------
int App::run() {
  dpi::enableAwareness();  // 첫 창 생성 전에 Per-Monitor V2
                           // 활성화(폰트/레이아웃 선명)
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv) {
    if (argc > 1 && argv[1] && argv[1][0]) pendingOpen_ = argv[1];
    LocalFree(argv);
  }

  settings_.load();
  theme_.setMode(settings_.theme);
  view_ = (settings_.defaultView == "editor")    ? 0
          : (settings_.defaultView == "preview") ? 2
                                                 : 1;
  editBrush_.reset(CreateSolidBrush(theme_.bg()));
  dpi_ = dpi::forWindow(nullptr);  // 주 모니터 DPI (창 생성 전, 화면 기준)
  makeUiFonts();

  HINSTANCE hInst = GetModuleHandleW(nullptr);
  HICON hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON),
                                  IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                  GetSystemMetrics(SM_CYICON), 0);
  HICON hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON),
                                    IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), 0);

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = App::MainProc;
  wc.hInstance = hInst;
  wc.lpszClassName = L"MyMDMain";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)editBrush_.get();
  wc.hIcon = hIcon;
  wc.hIconSm = hIconSm;
  RegisterClassExW(&wc);

  hwnd_ = CreateWindowExW(0, L"MyMDMain", L"MyMD",
                          WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                          CW_USEDEFAULT, dpiScale(1120), dpiScale(740), nullptr,
                          nullptr, hInst, this);
  // 프레임리스 적용 (WM_NCCALCSIZE 가 비클라이언트를 제거하도록 프레임 변경
  // 통지)
  SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

  wireCallbacks();

  // 실행 인자 파일 로드 (창 표시 전)
  if (!pendingOpen_.empty()) {
    std::string content;
    if (readFile(pendingOpen_, content)) {
      setCurrentFile(pendingOpen_);
      editor_.setTextUtf8Lf(content);
      markSaved();
    }
  }
  updateTitle();

  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);  // 에디터 즉시 페인트
  SetFocus(editor_.hwnd());
  layout();  // 컨트롤 배치(엔진 없이도 안전)
  // 엔진 콜드 부팅은 동기 블로킹이라 시작 경로에서 빼고, 메시지 루프가 도는
  // 첫 유휴에 백그라운드로 프리웜한다(편집 전용 기본값도 미리 띄워 진입을
  // 즉시화).
  PostMessageW(hwnd_, WM_APP_PREWARM, 0, 0);

  rebuildAccel();  // settings_.keymapJson 기반 액셀러레이터

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (!TranslateAcceleratorW(hwnd_, hAccel_, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  if (hAccel_) DestroyAcceleratorTable(hAccel_);
  return 0;
}

void App::rebuildAccel() {
  std::vector<ACCEL> a = keymap::buildAccels(settings_.keymapJson);
  HACCEL h = CreateAcceleratorTableW(a.data(), (int)a.size());
  if (h) {
    if (hAccel_) DestroyAcceleratorTable(hAccel_);
    hAccel_ = h;
  }
}
