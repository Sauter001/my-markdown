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
#include <vector>
#include "webview.h"
#include "resource.h"

// 명령 ID (단축키/메뉴)
#define IDM_NEW    101
#define IDM_OPEN   102
#define IDM_SAVE   103
#define IDM_SAVEAS 104
#define IDT_RENDER 1   // 프리뷰 디바운스 타이머

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

// 설정 (settings.json)
static int          g_fontSize = 14;
static int          g_tabSize  = 4;
static bool         g_wrap     = true;
static std::string  g_theme    = "system"; // system/light/dark
static std::string  g_defaultView = "split";
static std::string  g_langsJson = "[\"bash\",\"c\",\"cpp\",\"java\",\"python\",\"html\",\"css\",\"javascript\",\"sql\",\"json\"]";
static std::wstring g_curDir;   // 현재 문서 폴더 (이미지 base)

// 프리뷰 (WebView2, 지연 생성)
static HWND g_preview = nullptr;                  // 프리뷰 호스트 자식 창
static webview::webview *g_webview = nullptr;     // WebView2 엔진 (split/preview 진입 시 생성)
static bool g_webviewReady = false;               // preview.js 준비 완료
static int g_view = 1;            // 0 에디터, 1 분할, 2 미리보기
static double g_splitRatio = 0.5; // 분할 보기 에디터 비율
static bool g_divDrag = false;    // 디바이더 드래그 중
static int g_dividerX = -1;       // 디바이더 좌표 (분할 보기, 아니면 -1)
static const int kDividerW = 5;

static void refreshPreview(); // 전방 선언 (정의는 프리뷰 섹션)

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
// base64 (네이티브 <-> 프리뷰 브리지용)
// ---------------------------------------------------------------------------
static const char *B64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string base64_encode(const std::string &in) {
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= in.size()) {
    unsigned a = (unsigned char)in[i], b = (unsigned char)in[i + 1], c = (unsigned char)in[i + 2];
    unsigned n = (a << 16) | (b << 8) | c;
    out += B64[(n >> 18) & 63]; out += B64[(n >> 12) & 63];
    out += B64[(n >> 6) & 63];  out += B64[n & 63];
    i += 3;
  }
  if (i + 1 == in.size()) {
    unsigned n = (unsigned char)in[i] << 16;
    out += B64[(n >> 18) & 63]; out += B64[(n >> 12) & 63]; out += '='; out += '=';
  } else if (i + 2 == in.size()) {
    unsigned n = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
    out += B64[(n >> 18) & 63]; out += B64[(n >> 12) & 63]; out += B64[(n >> 6) & 63]; out += '=';
  }
  return out;
}
static int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
static std::string base64_decode(const std::string &in) {
  std::string out; out.reserve((in.size() / 4) * 3);
  int buf = 0, bits = 0;
  for (char c : in) {
    if (c == '=') break;
    int v = b64val(c);
    if (v < 0) continue;
    buf = (buf << 6) | v; bits += 6;
    if (bits >= 8) { bits -= 8; out += (char)((buf >> bits) & 0xFF); }
  }
  return out;
}
// 브리지 인자(base64 문자열)에서 첫 따옴표 문자열 추출
static std::string firstStringArg(const std::string &req) {
  size_t a = req.find('"');
  if (a == std::string::npos) return std::string();
  size_t b = req.find('"', a + 1);
  if (b == std::string::npos) return std::string();
  return req.substr(a + 1, b - a - 1);
}
// file:/// URL 생성 (UTF-8 퍼센트 인코딩)
static std::string toFileUrl(const std::wstring &path) {
  std::string utf8 = wide_to_utf8(path);
  std::string out = "file:///";
  for (unsigned char c : utf8) {
    if (c == '\\') { out += '/'; continue; }
    bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                c == '~' || c == '/' || c == ':';
    if (keep) out += (char)c;
    else { char b[4]; sprintf(b, "%%%02X", c); out += b; }
  }
  return out;
}

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

static std::wstring dirOf(const std::wstring &p) {
  size_t i = p.find_last_of(L"\\/");
  return (i == std::wstring::npos) ? std::wstring() : p.substr(0, i);
}
static void setCurrentFile(const std::wstring &path) {
  g_curPath = path;
  g_curName = baseName(path);
  g_curDir  = dirOf(path);
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
static std::string jsonArrayRaw(const std::string &j, const char *key, const std::string &dft) {
  size_t p = jsonValuePos(j, key);
  if (p == std::string::npos || p >= j.size() || j[p] != '[') return dft;
  int depth = 0;
  for (size_t i = p; i < j.size(); i++) {
    if (j[i] == '[') depth++;
    else if (j[i] == ']') { if (--depth == 0) return j.substr(p, i - p + 1); }
  }
  return dft;
}
static void loadSettings() {
  std::string j;
  if (!readFile(settingsPath(), j)) return; // 없으면 기본값 유지
  g_fontSize    = jsonInt(j, "fontSize", g_fontSize);
  g_tabSize     = jsonInt(j, "tabSize", g_tabSize);
  g_wrap        = jsonBool(j, "wrap", g_wrap);
  g_theme       = jsonStr(j, "theme", g_theme);
  g_defaultView = jsonStr(j, "defaultView", g_defaultView);
  g_langsJson   = jsonArrayRaw(j, "highlightLanguages", g_langsJson);
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

// 상단바 테마색
static COLORREF themeTopbarBg() { return isDarkTheme() ? RGB(0x16, 0x1b, 0x22) : RGB(0xf6, 0xf8, 0xfa); }
static COLORREF themeBorder()   { return isDarkTheme() ? RGB(0x30, 0x36, 0x3d) : RGB(0xe1, 0xe4, 0xe8); }
static COLORREF themeBtnHover() { return isDarkTheme() ? RGB(0x30, 0x36, 0x3d) : RGB(0xee, 0xf1, 0xf4); }
static COLORREF themeMuted()    { return isDarkTheme() ? RGB(0x8b, 0x94, 0x9e) : RGB(0x6e, 0x77, 0x81); }
static COLORREF themeAccent()   { return isDarkTheme() ? RGB(0x2f, 0x81, 0xf7) : RGB(0x09, 0x69, 0xda); }

// ---------------------------------------------------------------------------
// 네이티브 상단바 (프레임리스 창의 커스텀 타이틀바/툴바)
// ---------------------------------------------------------------------------
static const int kTopbarH = 38;
static const int kResizeBorder = 6;
static HFONT g_uiFont = nullptr;
static HFONT g_glyphFont = nullptr; // 창 제어 아이콘 (Segoe MDL2 Assets)

enum { IDM_MIN = 201, IDM_MAX = 202, IDM_WCLOSE = 203,
       IDM_VIEW_E = 301, IDM_VIEW_S = 302, IDM_VIEW_P = 303 };
struct TopBtn { int id; std::wstring label; RECT rc; int type; }; // type: 0 텍스트, 1 창제어, 2 닫기, 3 보기세그
static std::vector<TopBtn> g_btns;
static int g_hotBtn = -1;

static void invalidateTopbar() {
  if (!g_hwnd) return;
  RECT cr; GetClientRect(g_hwnd, &cr);
  RECT bar = { 0, 0, cr.right, kTopbarH };
  InvalidateRect(g_hwnd, &bar, FALSE);
}

// ---------------------------------------------------------------------------
// 창 제목 갱신
// ---------------------------------------------------------------------------
static void updateTitle() {
  std::wstring t;
  if (g_dirty) t += L"* ";
  t += g_curName.empty() ? W(u8"제목 없음") : g_curName;
  if (g_hwnd) SetWindowTextW(g_hwnd, t.c_str());
  invalidateTopbar(); // 파일명/더티 점 갱신
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
  refreshPreview(); // 경로(이미지 base) 변경 반영
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
  g_curDir.clear();
  setEditText("");
  g_dirty = false;
  updateTitle();
  refreshPreview();
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
  refreshPreview();
  SetFocus(g_edit);
}

// ---------------------------------------------------------------------------
// 상단바 레이아웃/그리기/히트테스트
// ---------------------------------------------------------------------------
static int textW(HDC dc, const std::wstring &s) {
  SIZE sz; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz); return sz.cx;
}
static void layoutTopbar(int width) {
  g_btns.clear();
  HDC dc = GetDC(g_hwnd);
  HFONT old = (HFONT)SelectObject(dc, g_uiFont);
  int x = width;
  const int wc = 46;
  // Segoe MDL2 Assets 캡션 글리프: 최소화 E921, 최대화 E922(복원 E923), 닫기 E8BB
  g_btns.push_back({ IDM_WCLOSE, L"\xE8BB", { x - wc, 0, x, kTopbarH }, 2 }); x -= wc;
  g_btns.push_back({ IDM_MAX,    L"\xE922", { x - wc, 0, x, kTopbarH }, 1 }); x -= wc;
  g_btns.push_back({ IDM_MIN,    L"\xE921", { x - wc, 0, x, kTopbarH }, 1 }); x -= wc;
  x -= 10;
  const int bh = 26, bt = (kTopbarH - bh) / 2;
  // 보기 모드 세그먼트 (미리/분할/편집)
  TopBtn views[] = {
    { IDM_VIEW_P, W(u8"미리"), {}, 3 },
    { IDM_VIEW_S, W(u8"분할"), {}, 3 },
    { IDM_VIEW_E, W(u8"편집"), {}, 3 },
  };
  for (TopBtn &v : views) {
    int w = textW(dc, v.label) + 16;
    v.rc = { x - w, bt, x, bt + bh };
    g_btns.push_back(v);
    x -= w + 2;
  }
  x -= 10;
  // 파일 버튼
  TopBtn items[] = {
    { IDM_SAVE, W(u8"저장"),   {}, 0 },
    { IDM_OPEN, W(u8"열기"),   {}, 0 },
    { IDM_NEW,  W(u8"새 파일"), {}, 0 },
  };
  for (TopBtn &it : items) {
    int w = textW(dc, it.label) + 18;
    it.rc = { x - w, bt, x, bt + bh };
    g_btns.push_back(it);
    x -= w + 4;
  }
  SelectObject(dc, old);
  ReleaseDC(g_hwnd, dc);
}
static int btnAt(int x, int y) {
  POINT p = { x, y };
  for (size_t i = 0; i < g_btns.size(); i++)
    if (PtInRect(&g_btns[i].rc, p)) return (int)i;
  return -1;
}
static int leftmostBtnX(int width) {
  int m = width;
  for (TopBtn &b : g_btns) m = std::min(m, (int)b.rc.left);
  return m;
}
static void paintTopbar(HDC dc, int width) {
  RECT bar = { 0, 0, width, kTopbarH };
  HBRUSH bg = CreateSolidBrush(themeTopbarBg());
  FillRect(dc, &bar, bg); DeleteObject(bg);
  HPEN pen = CreatePen(PS_SOLID, 1, themeBorder());
  HPEN oldPen = (HPEN)SelectObject(dc, pen);
  MoveToEx(dc, 0, kTopbarH - 1, nullptr); LineTo(dc, width, kTopbarH - 1);
  SelectObject(dc, oldPen); DeleteObject(pen);

  HFONT oldFont = (HFONT)SelectObject(dc, g_uiFont);
  SetBkMode(dc, TRANSPARENT);

  int fx = 12;
  if (g_dirty) {
    SetTextColor(dc, themeAccent());
    RECT dr = { fx, 0, fx + 14, kTopbarH };
    DrawTextW(dc, L"\x25CF", 1, &dr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    fx += 16;
  }
  SetTextColor(dc, themeFg());
  std::wstring name = g_curName.empty() ? W(u8"제목 없음") : g_curName;
  RECT nr = { fx, 0, leftmostBtnX(width) - 8, kTopbarH };
  DrawTextW(dc, name.c_str(), -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  for (size_t i = 0; i < g_btns.size(); i++) {
    TopBtn &b = g_btns[i];
    bool hot = ((int)i == g_hotBtn);
    bool active = false;
    if (b.type == 3) {
      int bv = (b.id == IDM_VIEW_E) ? 0 : (b.id == IDM_VIEW_S) ? 1 : 2;
      active = (bv == g_view);
    }
    if (active) {
      HBRUSH hb = CreateSolidBrush(themeAccent());
      FillRect(dc, &b.rc, hb); DeleteObject(hb);
    } else if (hot) {
      COLORREF hc = (b.type == 2) ? RGB(0xe8, 0x11, 0x23) : themeBtnHover();
      HBRUSH hb = CreateSolidBrush(hc);
      FillRect(dc, &b.rc, hb); DeleteObject(hb);
    }
    COLORREF tc;
    if (active) tc = RGB(0xff, 0xff, 0xff);
    else if (b.type == 0) tc = themeFg();
    else tc = hot ? (b.type == 2 ? RGB(0xff, 0xff, 0xff) : themeFg()) : themeMuted();
    SetTextColor(dc, tc);
    const wchar_t *glyph = b.label.c_str();
    if (b.id == IDM_MAX && IsZoomed(g_hwnd)) glyph = L"\xE923"; // 최대화 상태면 복원 아이콘
    SelectObject(dc, (b.type == 1 || b.type == 2) ? g_glyphFont : g_uiFont);
    DrawTextW(dc, glyph, -1, &b.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  SelectObject(dc, oldFont);
}

// ---------------------------------------------------------------------------
// 프리뷰 (WebView2) 브리지 + 보기 레이아웃
// ---------------------------------------------------------------------------
// 미리보기 외부 링크를 기본 브라우저로 (http/https/mailto 만 허용)
static std::string onOpenExternalBind(const std::string &req) {
  std::string url = base64_decode(firstStringArg(req));
  auto startsWith = [&](const char *p) {
    size_t n = std::strlen(p);
    return url.size() >= n && _strnicmp(url.c_str(), p, (int)n) == 0;
  };
  if (!startsWith("http://") && !startsWith("https://") && !startsWith("mailto:"))
    return "{\"ok\":false}";
  ShellExecuteW(g_hwnd, L"open", utf8_to_wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return "{\"ok\":true}";
}

static void pushPreviewNow() {
  if (!g_webview || !g_webviewReady) return;
  g_webview->eval("window.mymdRender&&window.mymdRender(\"" + base64_encode(getEditText()) + "\")");
}
static void pushPreviewConfig() {
  if (!g_webview || !g_webviewReady) return;
  g_webview->eval(std::string("window.mymdSetTheme&&window.mymdSetTheme(\"") +
                  (isDarkTheme() ? "dark" : "light") + "\")");
  g_webview->eval("window.mymdSetLangs&&window.mymdSetLangs(\"" + base64_encode(g_langsJson) + "\")");
  std::string base = g_curDir.empty() ? std::string() : (toFileUrl(g_curDir) + "/");
  g_webview->eval("window.mymdSetBase&&window.mymdSetBase(\"" + base64_encode(base) + "\")");
}
static void refreshPreview() {
  if (g_view == 0) return;
  pushPreviewConfig();
  pushPreviewNow();
}
static std::string onPreviewReady(const std::string &) {
  g_webviewReady = true;
  pushPreviewConfig();
  pushPreviewNow();
  return "true";
}
static void ensureWebview() {
  if (g_webview) return;
  g_webview = new webview::webview(false, (void *)&g_preview); // 외부 창(우측 패널)에 임베드
  g_webview->bind("mymdPreviewReady", [](std::string r) { return onPreviewReady(r); });
  g_webview->bind("mymdOpenExternal", [](std::string r) { return onOpenExternalBind(r); });
  g_webview->navigate(toFileUrl(exeDir() + L"\\web\\preview.html"));
  SetFocus(g_edit); // 생성 시 프리뷰로 간 포커스 복귀
}

// 보기 모드에 따라 에디터/디바이더/프리뷰 배치
static void layout() {
  if (!g_edit) return;
  RECT cr; GetClientRect(g_hwnd, &cr);
  int W = cr.right, H = cr.bottom;
  int top = kTopbarH, ch = H - kTopbarH; if (ch < 0) ch = 0;
  layoutTopbar(W);
  g_dividerX = -1;
  if (g_view == 0) {              // 에디터만
    MoveWindow(g_edit, 0, top, W, ch, TRUE); ShowWindow(g_edit, SW_SHOW);
    if (g_preview) ShowWindow(g_preview, SW_HIDE);
  } else if (g_view == 2) {       // 미리보기만
    ShowWindow(g_edit, SW_HIDE);
    if (g_preview) { MoveWindow(g_preview, 0, top, W, ch, TRUE); ShowWindow(g_preview, SW_SHOW); }
  } else {                        // 분할
    int ew = (int)(W * g_splitRatio), minw = 120;
    if (ew < minw) ew = minw;
    if (ew > W - minw - kDividerW) ew = W - minw - kDividerW;
    if (ew < 0) ew = 0;
    MoveWindow(g_edit, 0, top, ew, ch, TRUE); ShowWindow(g_edit, SW_SHOW);
    int px = ew + kDividerW;
    if (g_preview) { MoveWindow(g_preview, px, top, W - px, ch, TRUE); ShowWindow(g_preview, SW_SHOW); }
    g_dividerX = ew;
  }
  if (g_webview) g_webview->update_bounds();
  InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void setView(int v) {
  g_view = v;
  if (v != 0) ensureWebview(); // 에디터 전용이 아니면 프리뷰 엔진 지연 생성
  layout();
  refreshPreview();
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

static void runBtn(int id) {
  switch (id) {
    case IDM_NEW:    doNew();    break;
    case IDM_OPEN:   doOpen();   break;
    case IDM_SAVE:   doSave();   break;
    case IDM_SAVEAS: doSaveAs(); break;
    case IDM_MIN:    ShowWindow(g_hwnd, SW_MINIMIZE); break;
    case IDM_MAX:    ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE); break;
    case IDM_WCLOSE: SendMessageW(g_hwnd, WM_CLOSE, 0, 0); break;
    case IDM_VIEW_E: setView(0); break;
    case IDM_VIEW_S: setView(1); break;
    case IDM_VIEW_P: setView(2); break;
  }
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
      // 프리뷰 호스트 자식 창 (WebView2 는 지연 임베드, 초기엔 숨김)
      g_preview = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, h,
                                  (HMENU)(INT_PTR)2, GetModuleHandleW(nullptr), nullptr);
      return 0;
    }
    case WM_NCCALCSIZE:
      if (w == TRUE) {
        if (IsZoomed(h)) { // 최대화 시 작업표시줄을 덮지 않게 프레임만큼 보정
          int fx = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
          int fy = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
          NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)l;
          p->rgrc[0].left += fx; p->rgrc[0].right -= fx;
          p->rgrc[0].top += fy;  p->rgrc[0].bottom -= fy;
        }
        return 0; // 비클라이언트 제거 -> 전체가 클라이언트(프레임리스)
      }
      break;
    case WM_NCHITTEST: {
      RECT rc; GetWindowRect(h, &rc);
      int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
      if (!IsZoomed(h)) {
        bool L_ = x < rc.left + kResizeBorder, R_ = x >= rc.right - kResizeBorder;
        bool T_ = y < rc.top + kResizeBorder,  B_ = y >= rc.bottom - kResizeBorder;
        if (T_ && L_) return HTTOPLEFT;   if (T_ && R_) return HTTOPRIGHT;
        if (B_ && L_) return HTBOTTOMLEFT; if (B_ && R_) return HTBOTTOMRIGHT;
        if (L_) return HTLEFT; if (R_) return HTRIGHT; if (T_) return HTTOP; if (B_) return HTBOTTOM;
      }
      POINT cp = { x, y }; ScreenToClient(h, &cp);
      if (cp.y < kTopbarH) return btnAt(cp.x, cp.y) >= 0 ? HTCLIENT : HTCAPTION;
      return HTCLIENT;
    }
    case WM_ERASEBKGND:
      return 1; // 깜빡임 방지: 상단바는 WM_PAINT, 나머지는 EDIT 가 그림
    case WM_PAINT: {
      PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
      RECT cr; GetClientRect(h, &cr);
      paintTopbar(dc, cr.right);
      if (g_view == 1 && g_dividerX >= 0) { // 디바이더 스트립
        RECT dv = { g_dividerX, kTopbarH, g_dividerX + kDividerW, cr.bottom };
        HBRUSH b = CreateSolidBrush(themeBorder()); FillRect(dc, &dv, b); DeleteObject(b);
      }
      EndPaint(h, &ps);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      int mx = GET_X_LPARAM(l), my = GET_Y_LPARAM(l);
      if (g_view == 1 && g_dividerX >= 0 && mx >= g_dividerX && mx < g_dividerX + kDividerW && my >= kTopbarH) {
        SetCapture(h); g_divDrag = true; return 0;
      }
      int i = btnAt(mx, my);
      if (i >= 0) runBtn(g_btns[i].id);
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (g_divDrag) {
        RECT cr; GetClientRect(h, &cr);
        double r = cr.right > 0 ? (double)GET_X_LPARAM(l) / (double)cr.right : 0.5;
        if (r < 0.12) r = 0.12;
        if (r > 0.88) r = 0.88;
        g_splitRatio = r; layout();
        return 0;
      }
      int i = btnAt(GET_X_LPARAM(l), GET_Y_LPARAM(l));
      if (i != g_hotBtn) {
        g_hotBtn = i;
        invalidateTopbar();
        TRACKMOUSEEVENT tme = { sizeof(tme) };
        tme.dwFlags = TME_LEAVE; tme.hwndTrack = h;
        TrackMouseEvent(&tme);
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      if (g_hotBtn != -1) { g_hotBtn = -1; invalidateTopbar(); }
      return 0;
    case WM_LBUTTONUP:
      if (g_divDrag) { g_divDrag = false; ReleaseCapture(); }
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(l) == HTCLIENT && g_view == 1 && g_dividerX >= 0) {
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        if (p.x >= g_dividerX && p.x < g_dividerX + kDividerW && p.y >= kTopbarH) {
          SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE;
        }
      }
      break;
    case WM_TIMER:
      if (w == IDT_RENDER) { KillTimer(h, IDT_RENDER); pushPreviewNow(); }
      return 0;
    case WM_SIZE:
      layout();
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
        if (!g_suppressDirty) {
          if (!g_dirty) { g_dirty = true; updateTitle(); }
          if (g_view != 0) SetTimer(h, IDT_RENDER, 120, nullptr); // 프리뷰 디바운스
        }
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
  g_view = (g_defaultView == "editor") ? 0 : (g_defaultView == "preview") ? 2 : 1;
  g_editBrush = CreateSolidBrush(themeBg());
  g_uiFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
  g_glyphFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

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

  g_hwnd = CreateWindowExW(0, L"MyMDMain", L"MyMD",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, 1120, 740,
                           nullptr, nullptr, hInst, nullptr);
  // 프레임리스 적용 (WM_NCCALCSIZE 가 비클라이언트를 제거하도록 프레임 변경 통지)
  SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

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
  UpdateWindow(g_hwnd);          // 에디터 즉시 페인트
  SetFocus(g_edit);
  setView(g_view);               // 분할/미리보기면 여기서 프리뷰 엔진 지연 생성

  ACCEL accels[] = {
    { FCONTROL | FVIRTKEY, 'N', IDM_NEW },
    { FCONTROL | FVIRTKEY, 'O', IDM_OPEN },
    { FCONTROL | FVIRTKEY, 'S', IDM_SAVE },
    { FCONTROL | FSHIFT | FVIRTKEY, 'S', IDM_SAVEAS },
    { FCONTROL | FVIRTKEY, '1', IDM_VIEW_E },
    { FCONTROL | FVIRTKEY, '2', IDM_VIEW_S },
    { FCONTROL | FVIRTKEY, '3', IDM_VIEW_P },
  };
  HACCEL hAccel = CreateAcceleratorTableW(accels, 7);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (!TranslateAcceleratorW(g_hwnd, hAccel, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  return 0;
}
