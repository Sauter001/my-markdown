#include "core/str_util.h"

#include <windows.h>

#include <cstdio>

std::wstring utf8_to_wide(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}
std::string wide_to_utf8(const std::wstring& w) {
  if (w.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                              nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr,
                      nullptr);
  return s;
}
std::wstring W(const char* utf8) { return utf8_to_wide(utf8); }

static const char* B64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string base64_encode(const std::string& in) {
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= in.size()) {
    unsigned a = (unsigned char)in[i], b = (unsigned char)in[i + 1],
             c = (unsigned char)in[i + 2];
    unsigned n = (a << 16) | (b << 8) | c;
    out += B64[(n >> 18) & 63];
    out += B64[(n >> 12) & 63];
    out += B64[(n >> 6) & 63];
    out += B64[n & 63];
    i += 3;
  }
  if (i + 1 == in.size()) {
    unsigned n = (unsigned char)in[i] << 16;
    out += B64[(n >> 18) & 63];
    out += B64[(n >> 12) & 63];
    out += '=';
    out += '=';
  } else if (i + 2 == in.size()) {
    unsigned n = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
    out += B64[(n >> 18) & 63];
    out += B64[(n >> 12) & 63];
    out += B64[(n >> 6) & 63];
    out += '=';
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
std::string base64_decode(const std::string& in) {
  std::string out;
  out.reserve((in.size() / 4) * 3);
  int buf = 0, bits = 0;
  for (char c : in) {
    if (c == '=') break;
    int v = b64val(c);
    if (v < 0) continue;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += (char)((buf >> bits) & 0xFF);
    }
  }
  return out;
}
std::string firstStringArg(const std::string& req) {
  size_t a = req.find('"');
  if (a == std::string::npos) return std::string();
  size_t b = req.find('"', a + 1);
  if (b == std::string::npos) return std::string();
  return req.substr(a + 1, b - a - 1);
}
std::string toFileUrl(const std::wstring& path) {
  std::string utf8 = wide_to_utf8(path);
  std::string out = "file:///";
  for (unsigned char c : utf8) {
    if (c == '\\') {
      out += '/';
      continue;
    }
    bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                c == '~' || c == '/' || c == ':';
    if (keep)
      out += (char)c;
    else {
      char b[4];
      sprintf(b, "%%%02X", c);
      out += b;
    }
  }
  return out;
}

std::string crlfToLF(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\r') {
      o += '\n';
      if (i + 1 < s.size() && s[i + 1] == '\n') i++;
    } else
      o += s[i];
  }
  return o;
}
std::string lfToCRLF(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 16);
  for (char c : s) {
    if (c == '\n')
      o += "\r\n";
    else
      o += c;
  }
  return o;
}
