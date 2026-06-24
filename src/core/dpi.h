// DPI 유틸 (헤더 온리). 프로세스 DPI awareness 활성화 + 창 DPI 조회 + 픽셀 스케일.
// 모든 UI 픽셀 좌표는 96dpi 기준 논리값으로 두고, 사용 시점에 scale() 로 변환한다.
#pragma once
#include <windows.h>

namespace dpi {

// 프로세스를 Per-Monitor V2 DPI aware 로 만든다. 반드시 첫 top-level 창 생성 전에
// 호출해야 적용된다. 구형 OS 폴백: Per-Monitor(shcore) -> System(SetProcessDPIAware).
inline void enableAwareness() {
  if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
    typedef BOOL(WINAPI * SetCtx_t)(HANDLE);
    if (auto f = (SetCtx_t)GetProcAddress(u, "SetProcessDpiAwarenessContext")) {
      // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
      if (f((HANDLE)-4)) return;
    }
  }
  if (HMODULE sh = LoadLibraryW(L"shcore.dll")) {
    typedef HRESULT(WINAPI * SetAware_t)(int);
    if (auto f = (SetAware_t)GetProcAddress(sh, "SetProcessDpiAwareness")) {
      if (f(2) == S_OK) return;  // PROCESS_PER_MONITOR_DPI_AWARE
    }
  }
  SetProcessDPIAware();  // 최후 폴백(System DPI aware)
}

// 창의 DPI. GetDpiForWindow(Win10 1607+) 우선, 없으면 화면 DPI, 최후 96.
inline UINT forWindow(HWND h) {
  typedef UINT(WINAPI * GetDpiForWindow_t)(HWND);
  static GetDpiForWindow_t fn = []() -> GetDpiForWindow_t {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    return u ? (GetDpiForWindow_t)GetProcAddress(u, "GetDpiForWindow") : nullptr;
  }();
  if (fn && h) {
    UINT d = fn(h);
    if (d) return d;
  }
  HDC dc = GetDC(nullptr);
  UINT d = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSY) : 96;
  if (dc) ReleaseDC(nullptr, dc);
  return d ? d : 96;
}

// 96dpi 기준 논리 픽셀을 해당 DPI 의 실제 픽셀로 변환.
inline int scale(int px96, UINT dpi) { return MulDiv(px96, (int)dpi, 96); }

}  // namespace dpi
