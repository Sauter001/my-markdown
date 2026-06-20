#include "core/file_io.h"

#include <windows.h>

#include <algorithm>

#include "core/raii.h"

bool readFile(const std::wstring& path, std::string& out) {
  UniqueHandle h(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                             nullptr));
  if (!h.valid()) return false;
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(h.get(), &sz)) return false;
  out.resize((size_t)sz.QuadPart);
  size_t total = 0;
  bool ok = true;
  while (total < out.size()) {
    DWORD toRead = (DWORD)std::min(out.size() - total, (size_t)(1 << 20));
    DWORD rd = 0;
    if (!ReadFile(h.get(), &out[total], toRead, &rd, nullptr)) {
      ok = false;
      break;
    }
    if (rd == 0) break;
    total += rd;
  }
  if (ok) out.resize(total);
  // UTF-8 BOM 제거
  if (out.size() >= 3 && (unsigned char)out[0] == 0xEF &&
      (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
    out.erase(0, 3);
  return ok;
}

bool writeFile(const std::wstring& path, const std::string& bytes) {
  UniqueHandle h(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!h.valid()) return false;
  size_t total = 0;
  bool ok = true;
  while (total < bytes.size()) {
    DWORD toW = (DWORD)std::min(bytes.size() - total, (size_t)(1 << 20));
    DWORD wr = 0;
    if (!WriteFile(h.get(), bytes.data() + total, toW, &wr, nullptr)) {
      ok = false;
      break;
    }
    total += wr;
  }
  return ok;
}

std::wstring baseName(const std::wstring& p) {
  size_t i = p.find_last_of(L"\\/");
  return (i == std::wstring::npos) ? p : p.substr(i + 1);
}
std::wstring exeDir() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring p(buf, n);
  size_t i = p.find_last_of(L"\\/");
  return (i == std::wstring::npos) ? std::wstring() : p.substr(0, i);
}
std::wstring settingsPath() { return exeDir() + L"\\settings.json"; }
std::wstring dirOf(const std::wstring& p) {
  size_t i = p.find_last_of(L"\\/");
  return (i == std::wstring::npos) ? std::wstring() : p.substr(0, i);
}
