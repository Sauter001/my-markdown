// MyMD - 간이 마크다운 에디터 (네이티브 Win32 + WebView2 호스트)
//
// UI 전체는 WebView2 안의 HTML/JS(app)이고, C++는 다음만 담당한다:
//   - 네이티브 창 생성(webview 라이브러리가 대행)
//   - 네이티브 파일 열기/저장 대화상자 및 파일 입출력
//   - 창 제목(파일명/수정상태) 갱신
//   - 설정(settings.json) 영속화
//
// JS <-> C++ 브리지는 webview.bind 사용. 인코딩 문제를 피하려고
// 모든 동적 문자열(내용, 경로, 파일명, 설정)은 base64로 주고받는다.

#include <windows.h>
#include <windowsx.h>
#include <string>
#include <cstdio>
#include <algorithm>
#include "webview.h"
#include "resource.h"

// ---------------------------------------------------------------------------
// 전역 문서 상태
// ---------------------------------------------------------------------------
static std::wstring g_curPath;   // 현재 파일 전체 경로(비어 있으면 제목 없음)
static std::wstring g_curName;   // 제목 표시용 파일명
static bool         g_dirty = false;
static HWND         g_hwnd  = nullptr;
static std::wstring g_pendingOpen; // 실행 인자로 전달된 파일
static bool         g_forceClose = false;     // 저장 확인을 우회하는 강제 종료 플래그
static webview::webview *g_webview = nullptr;  // WM_CLOSE에서 JS eval 호출용

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

// ---------------------------------------------------------------------------
// base64
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
    unsigned a = (unsigned char)in[i];
    unsigned n = a << 16;
    out += B64[(n >> 18) & 63]; out += B64[(n >> 12) & 63];
    out += '='; out += '=';
  } else if (i + 2 == in.size()) {
    unsigned a = (unsigned char)in[i], b = (unsigned char)in[i + 1];
    unsigned n = (a << 16) | (b << 8);
    out += B64[(n >> 18) & 63]; out += B64[(n >> 12) & 63];
    out += B64[(n >> 6) & 63];  out += '=';
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
  std::string out;
  out.reserve((in.size() / 4) * 3);
  int buf = 0, bits = 0;
  for (char c : in) {
    if (c == '=' ) break;
    int v = b64val(c);
    if (v < 0) continue; // 공백/개행 등 무시
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += (char)((buf >> bits) & 0xFF);
    }
  }
  return out;
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
// 창 제목 갱신
// ---------------------------------------------------------------------------
static void updateTitle() {
  std::wstring t;
  if (g_dirty) t += L"* ";
  t += g_curName.empty() ? utf8_to_wide("\xEC\xA0\x9C\xEB\xAA\xA9 \xEC\x97\x86\xEC\x9D\x8C") /* 제목 없음 */
                         : g_curName;
  if (g_hwnd) SetWindowTextW(g_hwnd, t.c_str());
}

// ---------------------------------------------------------------------------
// 프레임리스 창 (기본 타이틀바 제거, 리사이즈/이동 직접 처리)
// ---------------------------------------------------------------------------
static WNDPROC g_origProc = nullptr;
static const int kResizeBorder = 6; // 가장자리 리사이즈 감지 폭(px)

static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCCALCSIZE:
      if (wp == TRUE) {
        if (IsZoomed(hwnd)) {
          // 최대화 시 작업표시줄을 덮지 않도록 프레임만큼 안쪽으로 보정
          int fx = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
          int fy = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
          NCCALCSIZE_PARAMS *p = reinterpret_cast<NCCALCSIZE_PARAMS *>(lp);
          p->rgrc[0].left += fx; p->rgrc[0].right -= fx;
          p->rgrc[0].top += fy;  p->rgrc[0].bottom -= fy;
        }
        return 0; // 비클라이언트(타이틀바/테두리) 제거 -> 전체가 클라이언트
      }
      break;
    case WM_NCHITTEST: {
      if (IsZoomed(hwnd)) return HTCLIENT;
      RECT rc; GetWindowRect(hwnd, &rc);
      int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
      bool l = x < rc.left + kResizeBorder,  r = x >= rc.right - kResizeBorder;
      bool t = y < rc.top + kResizeBorder,   b = y >= rc.bottom - kResizeBorder;
      if (t && l) return HTTOPLEFT;
      if (t && r) return HTTOPRIGHT;
      if (b && l) return HTBOTTOMLEFT;
      if (b && r) return HTBOTTOMRIGHT;
      if (l) return HTLEFT;
      if (r) return HTRIGHT;
      if (t) return HTTOP;
      if (b) return HTBOTTOM;
      return HTCLIENT;
    }
    case WM_CLOSE:
      // 저장하지 않은 변경이 있으면 닫기를 보류하고 JS 확인 모달을 띄운다.
      if (!g_forceClose && g_dirty) {
        if (g_webview) g_webview->eval("window.mymdOnCloseRequest && window.mymdOnCloseRequest();");
        return 0;
      }
      break;
  }
  return CallWindowProcW(g_origProc, hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 브리지 도우미
// ---------------------------------------------------------------------------
// req(파라미터 배열 텍스트)에서 첫 번째 따옴표 문자열을 추출.
// 내용은 항상 base64라 따옴표/역슬래시가 없으므로 단순 추출로 안전.
static std::string firstStringArg(const std::string &req) {
  size_t a = req.find('"');
  if (a == std::string::npos) return std::string();
  size_t b = req.find('"', a + 1);
  if (b == std::string::npos) return std::string();
  return req.substr(a + 1, b - a - 1);
}
// "name":"<base64(utf8(w))>" 형태의 JSON 필드
static std::string b64Field(const char *name, const std::wstring &w) {
  return std::string("\"") + name + "\":\"" + base64_encode(wide_to_utf8(w)) + "\"";
}
static std::string b64Field(const char *name, const std::string &bytes) {
  return std::string("\"") + name + "\":\"" + base64_encode(bytes) + "\"";
}

static void setCurrentFile(const std::wstring &path) {
  g_curPath = path;
  g_curName = baseName(path);
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
// 브리지 핸들러 (모두 동기 bind: std::string(JSON) 반환)
// ---------------------------------------------------------------------------
static std::string onReady(const std::string &) {
  if (g_pendingOpen.empty()) return "{}";
  std::string content;
  std::wstring path = g_pendingOpen;
  g_pendingOpen.clear();
  if (!readFile(path, content)) return "{}";
  setCurrentFile(path);
  g_dirty = false;
  updateTitle();
  return "{\"ok\":true," + b64Field("nameB64", g_curName) + "," +
         b64Field("pathB64", g_curPath) + "," + b64Field("contentB64", content) + "}";
}

static std::string onOpen(const std::string &) {
  std::wstring path;
  if (!openDialog(path)) return "{\"cancelled\":true}";
  std::string content;
  if (!readFile(path, content)) return "{\"error\":\"read failed\"}";
  setCurrentFile(path);
  g_dirty = false;
  updateTitle();
  return "{\"ok\":true," + b64Field("nameB64", g_curName) + "," +
         b64Field("pathB64", g_curPath) + "," + b64Field("contentB64", content) + "}";
}

static std::string onSave(const std::string &req) {
  std::string content = base64_decode(firstStringArg(req));
  if (g_curPath.empty()) return "{\"needSaveAs\":true}";
  if (!writeFile(g_curPath, content)) return "{\"error\":\"write failed\"}";
  g_dirty = false;
  updateTitle();
  return "{\"ok\":true}";
}

static std::string onSaveAs(const std::string &req) {
  std::string content = base64_decode(firstStringArg(req));
  std::wstring suggested = g_curName.empty() ? L"untitled.md" : g_curName;
  std::wstring path;
  if (!saveDialog(suggested, path)) return "{\"cancelled\":true}";
  if (!writeFile(path, content)) return "{\"error\":\"write failed\"}";
  setCurrentFile(path);
  g_dirty = false;
  updateTitle();
  return "{\"ok\":true," + b64Field("nameB64", g_curName) + "," +
         b64Field("pathB64", g_curPath) + "}";
}

static std::string onNew(const std::string &) {
  g_curPath.clear();
  g_curName.clear();
  g_dirty = false;
  updateTitle();
  return "{\"ok\":true}";
}

static std::string onSetDirty(const std::string &req) {
  g_dirty = (req.find("true") != std::string::npos);
  updateTitle();
  return "true";
}

static std::string onLoadSettings(const std::string &) {
  std::string content;
  if (!readFile(settingsPath(), content)) return "{}";
  return "{" + b64Field("b64", content) + "}";
}

static std::string onSaveSettings(const std::string &req) {
  std::string content = base64_decode(firstStringArg(req));
  if (!writeFile(settingsPath(), content)) return "{\"error\":\"write failed\"}";
  return "true";
}

// 창 제어 (프레임리스라 직접 제공)
static std::string onDragMove(const std::string &) {
  ReleaseCapture();
  SendMessageW(g_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
  return "true";
}
static std::string onMinimize(const std::string &) {
  ShowWindow(g_hwnd, SW_MINIMIZE);
  return "true";
}
static std::string onToggleMax(const std::string &) {
  ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE);
  return "true";
}
static std::string onClose(const std::string &) {
  PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
  return "true";
}
// 저장 확인을 건너뛰고 강제 종료 (JS에서 "저장 안 함" 선택 시)
static std::string onForceClose(const std::string &) {
  g_forceClose = true;
  PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
  return "true";
}

// ---------------------------------------------------------------------------
// 진입점
// ---------------------------------------------------------------------------
int main() {
  // 실행 인자(파일 경로) 파싱
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv) {
    if (argc > 1 && argv[1] && argv[1][0]) g_pendingOpen = argv[1];
    LocalFree(argv);
  }

#ifdef MYMD_DEBUG
  webview::webview w(true, nullptr); // 개발 중 devtools 사용
#else
  webview::webview w(false, nullptr);
#endif
  g_hwnd = (HWND)w.window();
  g_webview = &w;

  // 창 아이콘(작업표시줄/Alt+Tab) 설정. exe 에 임베드된 favicon 사용.
  HINSTANCE hInst = GetModuleHandleW(nullptr);
  HICON hIconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                     GetSystemMetrics(SM_CXICON),
                                     GetSystemMetrics(SM_CYICON), 0);
  HICON hIconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                       GetSystemMetrics(SM_CXSMICON),
                                       GetSystemMetrics(SM_CYSMICON), 0);
  if (hIconBig)   SendMessageW(g_hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIconBig);
  if (hIconSmall) SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
  if (hIconBig)   SetClassLongPtrW(g_hwnd, GCLP_HICON,   (LONG_PTR)hIconBig);
  if (hIconSmall) SetClassLongPtrW(g_hwnd, GCLP_HICONSM, (LONG_PTR)hIconSmall);

  // 기본 타이틀바 제거 (프레임리스). 창 제어는 인앱 상단바에서 처리.
  g_origProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)SubclassProc);
  SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

  w.set_size(1120, 740, 0 /* WEBVIEW_HINT_NONE: 초기 크기, 자유 리사이즈 */);
  w.set_size(480, 320, 1 /* WEBVIEW_HINT_MIN: 최소 크기 */);

  // 브리지 등록 (navigate 이전이어야 문서 생성 시 주입됨)
  w.bind("mymdReady",        [](std::string r) { return onReady(r); });
  w.bind("mymdOpen",         [](std::string r) { return onOpen(r); });
  w.bind("mymdSave",         [](std::string r) { return onSave(r); });
  w.bind("mymdSaveAs",       [](std::string r) { return onSaveAs(r); });
  w.bind("mymdNew",          [](std::string r) { return onNew(r); });
  w.bind("mymdSetDirty",     [](std::string r) { return onSetDirty(r); });
  w.bind("mymdLoadSettings", [](std::string r) { return onLoadSettings(r); });
  w.bind("mymdSaveSettings", [](std::string r) { return onSaveSettings(r); });
  w.bind("mymdDragMove",     [](std::string r) { return onDragMove(r); });
  w.bind("mymdMinimize",     [](std::string r) { return onMinimize(r); });
  w.bind("mymdToggleMax",    [](std::string r) { return onToggleMax(r); });
  w.bind("mymdClose",        [](std::string r) { return onClose(r); });
  w.bind("mymdForceClose",   [](std::string r) { return onForceClose(r); });

  std::wstring index = exeDir() + L"\\web\\index.html";
  w.navigate(toFileUrl(index));

  updateTitle();
  w.run();
  return 0;
}
