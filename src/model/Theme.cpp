#include "model/Theme.h"

bool Theme::isDark() const {
  if (mode_ == "dark") return true;
  if (mode_ == "light") return false;
  DWORD val = 1,
        sz = sizeof(val);  // system: 레지스트리 AppsUseLightTheme(0=다크)
  HKEY hk;
  if (RegOpenKeyExW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          0, KEY_READ, &hk) == ERROR_SUCCESS) {
    RegQueryValueExW(hk, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&val,
                     &sz);
    RegCloseKey(hk);
  }
  return val == 0;
}
