#include "core/markdown.h"

#include <algorithm>

// --- 공통 ---
std::wstring rtrimWs(const std::wstring& s) {
  size_t e = s.size();
  while (e > 0 && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' ||
                   s[e - 1] == L'\n'))
    e--;
  return s.substr(0, e);
}

// --- 리스트 ---
bool parseListItem(const std::wstring& line, ListItem& it) {
  size_t i = 0;
  while (i < line.size() && (line[i] == L' ' || line[i] == L'\t')) i++;
  it.indent = line.substr(0, i);
  if (i < line.size() &&
      (line[i] == L'-' || line[i] == L'*' || line[i] == L'+')) {
    size_t k = i + 1;
    if (k < line.size() && line[k] == L' ') {
      it.ordered = false;
      return true;
    }
  }
  size_t j = i;
  while (j < line.size() && line[j] >= L'0' && line[j] <= L'9') j++;
  if (j > i && j < line.size() && (line[j] == L'.' || line[j] == L')')) {
    size_t k = j + 1;
    if (k < line.size() && line[k] == L' ') {
      it.ordered = true;
      it.delim = line[j];
      it.num = 0;
      for (size_t t = i; t < j; t++) it.num = it.num * 10 + (line[t] - L'0');
      while (k < line.size() && line[k] == L' ') k++;
      it.rest = line.substr(k);
      return true;
    }
  }
  return false;
}
int leadingWsLen(const std::wstring& s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) i++;
  return (int)i;
}
int orderedStartNum(const std::vector<std::wstring>& lines, int curIdx,
                    int newIndentLen) {
  for (int i = curIdx - 1; i >= 0; i--) {
    if (rtrimWs(lines[i]).empty()) continue;
    int len = leadingWsLen(lines[i]);
    if (len < newIndentLen) return 1;  // 부모 레벨 도달
    if (len == newIndentLen) {
      ListItem it;
      if (parseListItem(lines[i], it) && it.ordered) return it.num + 1;
      return 1;
    }
    // len > newIndentLen: 더 깊음, 건너뜀
  }
  return 1;
}
std::vector<std::wstring> splitLines(const std::wstring& text) {
  std::vector<std::wstring> lines;
  size_t st = 0;
  for (size_t i = 0; i <= text.size(); i++) {
    if (i == text.size() || text[i] == L'\n') {
      std::wstring ln = text.substr(st, i - st);
      if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
      lines.push_back(ln);
      st = i + 1;
    }
  }
  return lines;
}

// --- 표 ---
std::wstring tblTrim(const std::wstring& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == L' ' || s[a] == L'\t')) a++;
  while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t')) b--;
  return s.substr(a, b - a);
}
std::vector<TblLine> tblGetLines(const std::wstring& val) {
  std::vector<TblLine> out;
  DWORD start = 0;
  for (DWORD i = 0; i <= (DWORD)val.size(); i++) {
    if (i == (DWORD)val.size() || val[i] == L'\n') {
      DWORD end = i;
      if (end > start && val[end - 1] == L'\r') end--;  // 줄 내용은 \r 제외
      out.push_back({val.substr(start, end - start), start});
      start = i + 1;
    }
  }
  return out;
}
int tblLineIndexAt(const std::vector<TblLine>& lines, DWORD pos) {
  for (int k = 0; k < (int)lines.size(); k++) {
    const TblLine& L = lines[k];
    if (pos >= L.start && pos <= L.start + (DWORD)L.text.size()) return k;
  }
  return (int)lines.size() - 1;
}

bool tblIsRow(const std::wstring& text) {
  std::wstring t = tblTrim(text);
  if (t.find(L'|') == std::wstring::npos) return false;
  if (!t.empty() && t[0] == L'|') return true;
  int cnt = 0;
  for (wchar_t c : t)
    if (c == L'|') cnt++;
  return cnt >= 2;
}

std::vector<std::wstring> tblParseCells(const std::wstring& line) {
  std::wstring t = tblTrim(line);
  if (!t.empty() && t.front() == L'|') t = t.substr(1);
  if (!t.empty() && t.back() == L'|') t = t.substr(0, t.size() - 1);
  std::vector<std::wstring> cells;
  size_t s = 0;
  for (size_t i = 0; i <= t.size(); i++) {
    if (i == t.size() || t[i] == L'|') {
      cells.push_back(tblTrim(t.substr(s, i - s)));
      s = i + 1;
    }
  }
  return cells;
}
bool tblIsSep(const std::wstring& text) {
  std::vector<std::wstring> cells = tblParseCells(text);
  if (cells.empty()) return false;
  for (auto& c0 : cells) {  // /^:?-{1,}:?$/
    std::wstring c = tblTrim(c0);
    size_t i = 0;
    if (i < c.size() && c[i] == L':') i++;
    size_t dash = 0;
    while (i < c.size() && c[i] == L'-') {
      i++;
      dash++;
    }
    if (dash < 1) return false;
    if (i < c.size() && c[i] == L':') i++;
    if (i != c.size()) return false;
  }
  return true;
}
std::vector<int> tblPipes(const std::wstring& text) {
  std::vector<int> p;
  for (int i = 0; i < (int)text.size(); i++)
    if (text[i] == L'|' && (i == 0 || text[i - 1] != L'\\')) p.push_back(i);
  return p;
}
void tblBounds(const std::vector<TblLine>& lines, int idx, int& top, int& bot) {
  top = idx;
  bot = idx;
  while (top > 0 && tblIsRow(lines[top - 1].text)) top--;
  while (bot < (int)lines.size() - 1 && tblIsRow(lines[bot + 1].text)) bot++;
}
static int tblChW(unsigned int c) {  // 전각(한글/CJK) 폭 2
  if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) ||
      (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) ||
      (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
      (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x20000 && c <= 0x3FFFD))
    return 2;
  return 1;
}
int tblW(const std::wstring& s) {
  int w = 0;
  for (size_t i = 0; i < s.size(); i++) {
    unsigned int c = (unsigned int)s[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size()) {  // 서로게이트 쌍 결합
      unsigned int lo = (unsigned int)s[i + 1];
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
        i++;
      }
    }
    w += tblChW(c);
  }
  return w;
}
std::wstring tblPad(const std::wstring& s, int n) {
  int t = n - tblW(s);
  if (t < 0) t = 0;
  return s + std::wstring((size_t)t, L' ');
}
std::wstring tblPadC(const std::wstring& s, int n) {
  int t = n - tblW(s);
  if (t < 0) t = 0;
  int l = t / 2;
  return std::wstring((size_t)l, L' ') + s +
         std::wstring((size_t)(t - l), L' ');
}
int tblPrevRow(const std::vector<TblLine>& lines, int from, int top) {
  for (int k = from; k >= top; k--)
    if (!tblIsSep(lines[k].text)) return k;
  return -1;
}
