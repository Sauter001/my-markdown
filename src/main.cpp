// MyMD - 간이 마크다운 에디터 (네이티브 Win32)
//
// Phase 2: 에디터를 WebView 밖 네이티브 EDIT 컨트롤로 분리한다(메모장급 즉시 로딩).
// 이 단계(2a)는 네이티브 셸 + 네이티브 에디터만 담당한다(프리뷰 없음).
//   - 네이티브 창 + 멀티라인 EDIT 컨트롤
//   - 파일 새로/열기/저장/다른이름저장 (네이티브 대화상자 + 입출력)
//   - 더티/제목, 설정(글꼴/탭/줄바꿈/테마) 적용, 단축키, 닫기 확인
// 프리뷰(WebView2)는 2c에서 우측 패널에 지연 임베드한다.

#include <windows.h>
#include <windowsx.h>
#include <string>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include "resource.h"

// 명령 ID (단축키/메뉴)
#define IDM_NEW    101
#define IDM_OPEN   102
#define IDM_SAVE   103
#define IDM_SAVEAS 104

// ---------------------------------------------------------------------------
// 전역 상태
// ---------------------------------------------------------------------------
static std::wstring g_curPath;     // 현재 파일 전체 경로(비어 있으면 제목 없음)
static std::wstring g_curName;     // 제목 표시용 파일명
static bool         g_dirty = false;
static HWND         g_hwnd  = nullptr;  // 메인 창
static HWND         g_edit  = nullptr;  // 에디터 EDIT 컨트롤
static HFONT        g_editFont = nullptr;
static HBRUSH       g_editBrush = nullptr;
static std::wstring g_pendingOpen;      // 실행 인자로 전달된 파일
static bool         g_suppressDirty = false; // 프로그램적 본문 설정 시 더티 무시

// 설정 (settings.json, 평면 필드만 2a에서 사용)
static int          g_fontSize = 14;
static int          g_tabSize  = 4;
static bool         g_wrap     = true;
static std::string  g_theme    = "system"; // system/light/dark

// ---------------------------------------------------------------------------
// 문자열 변환 (UTF-8 <-> UTF-16)
// ---------------------------------------------------------------------------
static std::wstring utf8_to_wide(const std::string &s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}
static std::string wide_to_utf8(const std::wstring &w) {
  if (w.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}
// 소스의 UTF-8 좁은 리터럴을 와이드로 (한글 UI 문자열용)
static std::wstring W(const char *utf8) { return utf8_to_wide(utf8); }

// ---------------------------------------------------------------------------
// 줄바꿈 정규화 (EDIT 는 CRLF, 파일은 LF 로 유지)
// ---------------------------------------------------------------------------
static std::string crlfToLF(const std::string &s) {
  std::string o; o.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\r') { o += '\n'; if (i + 1 < s.size() && s[i + 1] == '\n') i++; }
    else o += s[i];
  }
  return o;
}
static std::string lfToCRLF(const std::string &s) {
  std::string o; o.reserve(s.size() + 16);
  for (char c : s) { if (c == '\n') o += "\r\n"; else o += c; }
  return o;
}

// ---------------------------------------------------------------------------
// 파일 입출력 (와이드 경로)
// ---------------------------------------------------------------------------
static bool readFile(const std::wstring &path, std::string &out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return false; }
  out.resize((size_t)sz.QuadPart);
  size_t total = 0;
  bool ok = true;
  while (total < out.size()) {
    DWORD toRead = (DWORD)std::min(out.size() - total, (size_t)(1 << 20));
    DWORD rd = 0;
    if (!ReadFile(h, &out[total], toRead, &rd, nullptr)) { ok = false; break; }
    if (rd == 0) break;
    total += rd;
  }
  CloseHandle(h);
  if (ok) out.resize(total);
  // UTF-8 BOM 제거
  if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
      (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
    out.erase(0, 3);
  return ok;
}

static bool writeFile(const std::wstring &path, const std::string &bytes) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  size_t total = 0;
  bool ok = true;
  while (total < bytes.size()) {
    DWORD toW = (DWORD)std::min(bytes.size() - total, (size_t)(1 << 20));
    DWORD wr = 0;
    if (!WriteFile(h, bytes.data() + total, toW, &wr, nullptr)) { ok = false; break; }
    total += wr;
  }
  CloseHandle(h);
  return ok;
}

// ---------------------------------------------------------------------------
// 경로 도우미
// ---------------------------------------------------------------------------
static std::wstring baseName(const std::wstring &p) {
  size_t i = p.find_last_of(L"\\/");
  return (i == std::wstring::npos) ? p : p.substr(i + 1);
}
static std::wstring exeDir() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring p(buf, n);
  size_t i = p.find_last_of(L"\\/");
  return (i == std::wstring::npos) ? std::wstring() : p.substr(0, i);
}
static std::wstring settingsPath() { return exeDir() + L"\\settings.json"; }

static void setCurrentFile(const std::wstring &path) {
  g_curPath = path;
  g_curName = baseName(path);
}

// ---------------------------------------------------------------------------
// 설정 파싱 (평면 필드용 최소 파서)
// ---------------------------------------------------------------------------
static size_t jsonValuePos(const std::string &j, const char *key) {
  std::string pat = std::string("\"") + key + "\"";
  size_t k = j.find(pat);
  if (k == std::string::npos) return std::string::npos;
  size_t c = j.find(':', k + pat.size());
  if (c == std::string::npos) return std::string::npos;
  size_t p = c + 1;
  while (p < j.size() && (j[p] == ' ' || j[p] == '\t' || j[p] == '\n' || j[p] == '\r')) p++;
  return p;
}
static std::string jsonStr(const std::string &j, const char *key, const std::string &dft) {
  size_t p = jsonValuePos(j, key);
  if (p == std::string::npos || p >= j.size() || j[p] != '"') return dft;
  size_t q = j.find('"', p + 1);
  if (q == std::string::npos) return dft;
  return j.substr(p + 1, q - p - 1);
}
static int jsonInt(const std::string &j, const char *key, int dft) {
  size_t p = jsonValuePos(j, key);
  if (p == std::string::npos || p >= j.size()) return dft;
  int sign = 1;
  if (j[p] == '-') { sign = -1; p++; }
  if (p >= j.size() || !isdigit((unsigned char)j[p])) return dft;
  long v = 0;
  while (p < j.size() && isdigit((unsigned char)j[p])) { v = v * 10 + (j[p] - '0'); p++; }
  return (int)(sign * v);
}
static bool jsonBool(const std::string &j, const char *key, bool dft) {
  size_t p = jsonValuePos(j, key);
  if (p == std::string::npos) return dft;
  if (j.compare(p, 4, "true") == 0) return true;
  if (j.compare(p, 5, "false") == 0) return false;
  return dft;
}
static void loadSettings() {
  std::string j;
  if (!readFile(settingsPath(), j)) return; // 없으면 기본값 유지
  g_fontSize = jsonInt(j, "fontSize", g_fontSize);
  g_tabSize  = jsonInt(j, "tabSize", g_tabSize);
  g_wrap     = jsonBool(j, "wrap", g_wrap);
  g_theme    = jsonStr(j, "theme", g_theme);
  if (g_fontSize < 10) g_fontSize = 10;
  if (g_fontSize > 32) g_fontSize = 32;
  if (g_tabSize < 1) g_tabSize = 1;
  if (g_tabSize > 8) g_tabSize = 8;
}

// ---------------------------------------------------------------------------
// 테마
// ---------------------------------------------------------------------------
static bool isDarkTheme() {
  if (g_theme == "dark") return true;
  if (g_theme == "light") return false;
  DWORD val = 1, sz = sizeof(val); // system: 레지스트리 AppsUseLightTheme(0=다크)
  HKEY hk;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    0, KEY_READ, &hk) == ERROR_SUCCESS) {
    RegQueryValueExW(hk, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&val, &sz);
    RegCloseKey(hk);
  }
  return val == 0;
}
static COLORREF themeBg() { return isDarkTheme() ? RGB(0x0d, 0x11, 0x17) : RGB(0xff, 0xff, 0xff); }
static COLORREF themeFg() { return isDarkTheme() ? RGB(0xe6, 0xed, 0xf3) : RGB(0x1f, 0x23, 0x28); }

static HFONT makeEditFont(int px) {
  return CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     FIXED_PITCH | FF_MODERN, L"Consolas");
}

// ---------------------------------------------------------------------------
// 창 제목 갱신
// ---------------------------------------------------------------------------
static void updateTitle() {
  std::wstring t;
  if (g_dirty) t += L"* ";
  t += g_curName.empty() ? W("\xEC\xA0\x9C\xEB\xAA\xA9 \xEC\x97\x86\xEC\x9D\x8C") /* 제목 없음 */
                         : g_curName;
  if (g_hwnd) SetWindowTextW(g_hwnd, t.c_str());
}

// ---------------------------------------------------------------------------
// 에디터 본문 입출력
// ---------------------------------------------------------------------------
static std::string getEditText() {
  int len = GetWindowTextLengthW(g_edit);
  std::wstring w;
  w.resize(len + 1);
  int got = GetWindowTextW(g_edit, &w[0], len + 1);
  w.resize(got);
  return crlfToLF(wide_to_utf8(w)); // 파일에는 LF 로 저장
}
static void setEditText(const std::string &utf8_lf) {
  std::wstring w = utf8_to_wide(lfToCRLF(utf8_lf)); // EDIT 에는 CRLF 로
  g_suppressDirty = true;
  SetWindowTextW(g_edit, w.c_str());
  g_suppressDirty = false;
}

// ---------------------------------------------------------------------------
// 파일 대화상자
// ---------------------------------------------------------------------------
static bool openDialog(std::wstring &outPath) {
  wchar_t buf[4096] = {0};
  OPENFILENAMEW ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_hwnd;
  ofn.lpstrFilter =
      L"Markdown (*.md;*.markdown;*.txt)\0*.md;*.markdown;*.mdown;*.txt\0"
      L"All files (*.*)\0*.*\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = 4096;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetOpenFileNameW(&ofn)) { outPath = buf; return true; }
  return false;
}
static bool saveDialog(const std::wstring &suggested, std::wstring &outPath) {
  wchar_t buf[4096] = {0};
  if (!suggested.empty()) wcsncpy(buf, suggested.c_str(), 4095);
  OPENFILENAMEW ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_hwnd;
  ofn.lpstrFilter = L"Markdown (*.md)\0*.md\0All files (*.*)\0*.*\0";
  ofn.lpstrFile = buf;
  ofn.nMaxFile = 4096;
  ofn.lpstrDefExt = L"md";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetSaveFileNameW(&ofn)) { outPath = buf; return true; }
  return false;
}

// ---------------------------------------------------------------------------
// 파일 동작
// ---------------------------------------------------------------------------
static bool confirmDiscard() {
  if (!g_dirty) return true;
  int r = MessageBoxW(g_hwnd,
                      W("\xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEC\x9D\x80 \xEB\xB3\x80\xEA\xB2\xBD\xEC\x82\xAC\xED\x95\xAD\xEC\x9D\xB4 \xEC\x9E\x88\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4. \xEA\xB3\x84\xEC\x86\x8D\xED\x95\x98\xEC\x8B\x9C\xEA\xB2\xA0\xEC\x8A\xB5\xEB\x8B\x88\xEA\xB9\x8C?").c_str(),
                      L"MyMD", MB_YESNO | MB_ICONWARNING);
  return r == IDYES;
}

static bool doSaveAs() {
  std::wstring suggested = g_curName.empty() ? L"untitled.md" : g_curName;
  std::wstring path;
  if (!saveDialog(suggested, path)) return false;
  if (!writeFile(path, getEditText())) {
    MessageBoxW(g_hwnd, L"write failed", L"MyMD", MB_OK | MB_ICONERROR);
    return false;
  }
  setCurrentFile(path);
  g_dirty = false;
  updateTitle();
  return true;
}
static bool doSave() {
  if (g_curPath.empty()) return doSaveAs();
  if (!writeFile(g_curPath, getEditText())) {
    MessageBoxW(g_hwnd, L"write failed", L"MyMD", MB_OK | MB_ICONERROR);
    return false;
  }
  g_dirty = false;
  updateTitle();
  return true;
}
static void doNew() {
  if (!confirmDiscard()) return;
  g_curPath.clear();
  g_curName.clear();
  setEditText("");
  g_dirty = false;
  updateTitle();
  SetFocus(g_edit);
}
static void doOpen() {
  if (!confirmDiscard()) return;
  std::wstring path;
  if (!openDialog(path)) return;
  std::string content;
  if (!readFile(path, content)) {
    MessageBoxW(g_hwnd, L"read failed", L"MyMD", MB_OK | MB_ICONERROR);
    return;
  }
  setCurrentFile(path);
  setEditText(content);
  g_dirty = false;
  updateTitle();
  SetFocus(g_edit);
}

// ---------------------------------------------------------------------------
// 메인 창 프로시저
// ---------------------------------------------------------------------------
static void applyEditStyle() {
  if (g_editFont) DeleteObject(g_editFont);
  g_editFont = makeEditFont(g_fontSize);
  SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_editFont, TRUE);
  DWORD tw = (DWORD)(g_tabSize * 4); // 대략 N칸(다이얼로그 단위)
  SendMessageW(g_edit, EM_SETTABSTOPS, 1, (LPARAM)&tw);
  SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(10, 10));
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                    ES_NOHIDESEL | ES_WANTRETURN | ES_AUTOVSCROLL;
      if (!g_wrap) style |= ES_AUTOHSCROLL | WS_HSCROLL;
      g_edit = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 0, 0, h,
                               (HMENU)(INT_PTR)1, GetModuleHandleW(nullptr), nullptr);
      SendMessageW(g_edit, EM_SETLIMITTEXT, (WPARAM)0x7FFFFFFE, 0);
      applyEditStyle();
      return 0;
    }
    case WM_SIZE:
      if (g_edit) MoveWindow(g_edit, 0, 0, LOWORD(l), HIWORD(l), TRUE);
      return 0;
    case WM_SETFOCUS:
      if (g_edit) SetFocus(g_edit);
      return 0;
    case WM_CTLCOLOREDIT: {
      HDC dc = (HDC)w;
      SetTextColor(dc, themeFg());
      SetBkColor(dc, themeBg());
      return (LRESULT)g_editBrush;
    }
    case WM_COMMAND: {
      if ((HWND)l == g_edit && HIWORD(w) == EN_CHANGE) {
        if (!g_suppressDirty && !g_dirty) { g_dirty = true; updateTitle(); }
        return 0;
      }
      switch (LOWORD(w)) {
        case IDM_NEW:    doNew();    return 0;
        case IDM_OPEN:   doOpen();   return 0;
        case IDM_SAVE:   doSave();   return 0;
        case IDM_SAVEAS: doSaveAs(); return 0;
      }
      break;
    }
    case WM_GETMINMAXINFO: {
      MINMAXINFO *mmi = (MINMAXINFO *)l;
      mmi->ptMinTrackSize.x = 480;
      mmi->ptMinTrackSize.y = 320;
      return 0;
    }
    case WM_CLOSE:
      if (g_dirty) {
        int r = MessageBoxW(h,
            W("\xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEC\x9D\x80 \xEB\xB3\x80\xEA\xB2\xBD \xEC\x82\xAC\xED\x95\xAD\xEC\x9D\xB4 \xEC\x9E\x88\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4. \xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x98\xEC\x8B\x9C\xEA\xB2\xA0\xEC\x8A\xB5\xEB\x8B\x88\xEA\xB9\x8C?").c_str(),
            L"MyMD", MB_YESNOCANCEL | MB_ICONWARNING);
        if (r == IDCANCEL) return 0;
        if (r == IDYES && !doSave()) return 0; // 저장 실패/취소 시 닫지 않음
      }
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------------------
// 진입점
// ---------------------------------------------------------------------------
int main() {
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv) {
    if (argc > 1 && argv[1] && argv[1][0]) g_pendingOpen = argv[1];
    LocalFree(argv);
  }

  loadSettings();
  g_editBrush = CreateSolidBrush(themeBg());

  HINSTANCE hInst = GetModuleHandleW(nullptr);
  HICON hIcon   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
  HICON hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = MainProc;
  wc.hInstance = hInst;
  wc.lpszClassName = L"MyMDMain";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = g_editBrush;
  wc.hIcon = hIcon;
  wc.hIconSm = hIconSm;
  RegisterClassExW(&wc);

  g_hwnd = CreateWindowExW(0, L"MyMDMain", L"MyMD", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 1120, 740,
                           nullptr, nullptr, hInst, nullptr);

  // 실행 인자 파일 로드 (창 표시 전)
  if (!g_pendingOpen.empty()) {
    std::string content;
    if (readFile(g_pendingOpen, content)) {
      setCurrentFile(g_pendingOpen);
      setEditText(content);
      g_dirty = false;
    }
  }
  updateTitle();

  ShowWindow(g_hwnd, SW_SHOW);
  UpdateWindow(g_hwnd);
  SetFocus(g_edit);

  ACCEL accels[] = {
    { FCONTROL | FVIRTKEY, 'N', IDM_NEW },
    { FCONTROL | FVIRTKEY, 'O', IDM_OPEN },
    { FCONTROL | FVIRTKEY, 'S', IDM_SAVE },
    { FCONTROL | FSHIFT | FVIRTKEY, 'S', IDM_SAVEAS },
  };
  HACCEL hAccel = CreateAcceleratorTableW(accels, 4);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (!TranslateAcceleratorW(g_hwnd, hAccel, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  return 0;
}
