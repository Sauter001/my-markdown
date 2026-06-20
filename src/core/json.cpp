#include "core/json.h"

#include <cctype>
#include <cstdio>

static size_t jsonValuePos(const std::string& j, const char* key) {
  std::string pat = std::string("\"") + key + "\"";
  size_t k = j.find(pat);
  if (k == std::string::npos) return std::string::npos;
  size_t c = j.find(':', k + pat.size());
  if (c == std::string::npos) return std::string::npos;
  size_t p = c + 1;
  while (p < j.size() &&
         (j[p] == ' ' || j[p] == '\t' || j[p] == '\n' || j[p] == '\r'))
    p++;
  return p;
}

std::string jsonStr(const std::string& j, const char* key,
                    const std::string& dft) {
  size_t p = jsonValuePos(j, key);
  if (p == std::string::npos || p >= j.size() || j[p] != '"') return dft;
  size_t q = j.find('"', p + 1);
  if (q == std::string::npos) return dft;
  return j.substr(p + 1, q - p - 1);
}

int jsonInt(const std::string& j, const char* key, int dft) {
  size_t p = jsonValuePos(j, key);
  if (p == std::string::npos || p >= j.size()) return dft;
  int sign = 1;
  if (j[p] == '-') {
    sign = -1;
    p++;
  }
  if (p >= j.size() || !isdigit((unsigned char)j[p])) return dft;
  long v = 0;
  while (p < j.size() && isdigit((unsigned char)j[p])) {
    v = v * 10 + (j[p] - '0');
    p++;
  }
  return (int)(sign * v);
}

bool jsonBool(const std::string& j, const char* key, bool dft) {
  size_t p = jsonValuePos(j, key);
  if (p == std::string::npos) return dft;
  if (j.compare(p, 4, "true") == 0) return true;
  if (j.compare(p, 5, "false") == 0) return false;
  return dft;
}

std::string jsonArrayRaw(const std::string& j, const char* key,
                         const std::string& dft) {
  size_t p = jsonValuePos(j, key);
  if (p == std::string::npos || p >= j.size() || j[p] != '[') return dft;
  int depth = 0;
  for (size_t i = p; i < j.size(); i++) {
    if (j[i] == '[')
      depth++;
    else if (j[i] == ']') {
      if (--depth == 0) return j.substr(p, i - p + 1);
    }
  }
  return dft;
}

std::string jsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '"':
        o += "\\\"";
        break;
      case '\\':
        o += "\\\\";
        break;
      case '\n':
        o += "\\n";
        break;
      case '\r':
        o += "\\r";
        break;
      case '\t':
        o += "\\t";
        break;
      default:
        if ((unsigned char)c < 0x20) {
          char b[8];
          snprintf(b, sizeof(b), "\\u%04x", (unsigned)(unsigned char)c);
          o += b;
        } else
          o += c;
    }
  }
  return o;
}
