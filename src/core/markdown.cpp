#include "core/markdown.h"

#include <algorithm>
#include <map>

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

// --- 헤더 / 목차(TOC) ---
// 슬러그 규칙(web/preview.js 의 slugify 와 반드시 동일):
//   A-Z->소문자, a-z 0-9 _ 유지, 비ASCII(>=0x80, 한글/CJK 등) 유지,
//   공백류->'-', 그 외 ASCII 문장부호 삭제, 연속 '-' 합치고 앞뒤 '-' 제거.
std::wstring slugify(const std::wstring& raw) {
  std::wstring o;
  o.reserve(raw.size());
  for (wchar_t c : raw) {
    if (c >= L'A' && c <= L'Z')
      o += (wchar_t)(c - L'A' + L'a');
    else if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_')
      o += c;
    else if ((unsigned)c >= 0x80)
      o += c;
    else if (c == L' ' || c == L'\t')
      o += L'-';
    // 그 외 ASCII 문장부호: 삭제
  }
  std::wstring r;  // 연속 '-' 합치기
  r.reserve(o.size());
  for (wchar_t c : o) {
    if (c == L'-' && !r.empty() && r.back() == L'-') continue;
    r += c;
  }
  size_t a = 0, b = r.size();  // 앞뒤 '-' 제거
  while (a < b && r[a] == L'-') a++;
  while (b > a && r[b - 1] == L'-') b--;
  return r.substr(a, b - a);
}

// 줄 [s) 가 펜스(``` 또는 ~~~, 3개 이상, 선행 공백 허용)로 시작하는지.
static bool mdIsFenceLine(const std::wstring& ln) {
  size_t i = 0;
  while (i < ln.size() && (ln[i] == L' ' || ln[i] == L'\t')) i++;
  if (ln.size() - i < 3) return false;
  wchar_t c = ln[i];
  return (c == L'`' || c == L'~') && ln[i + 1] == c && ln[i + 2] == c;
}

std::vector<Heading> parseHeadings(const std::wstring& docText) {
  std::vector<Heading> out;
  std::map<std::wstring, int> seen;  // base slug -> 등장 횟수(중복 처리)
  bool inFence = false;
  for (const std::wstring& ln : splitLines(docText)) {
    if (mdIsFenceLine(ln)) {  // 펜스 여닫기 토글, 펜스 줄 자체도 헤더 아님
      inFence = !inFence;
      continue;
    }
    if (inFence) continue;
    size_t i = 0;
    while (i < ln.size() && ln[i] == L' ') i++;  // 선행 공백 허용
    if (i > 3) continue;  // 4칸 이상 들여쓰기는 코드블록(헤더 아님) - markdown-it 정합
    int level = 0;
    while (i < ln.size() && ln[i] == L'#') {
      level++;
      i++;
    }
    if (level < 1 || level > 6) continue;
    // # 뒤에는 공백 또는 줄끝이어야 헤더(예: "#tag" 제외)
    if (i < ln.size() && ln[i] != L' ' && ln[i] != L'\t') continue;
    std::wstring text = rtrimWs(ln.substr(i));  // 앞 공백/뒤 공백 제거
    size_t a = 0;
    while (a < text.size() && (text[a] == L' ' || text[a] == L'\t')) a++;
    text = text.substr(a);
    // 닫는 ATX(후행 # 들과 그 앞 공백) 제거
    size_t e = text.size();
    while (e > 0 && text[e - 1] == L'#') e--;
    if (e < text.size()) {  // 후행 # 가 있었으면 그 앞 공백도 제거
      while (e > 0 && (text[e - 1] == L' ' || text[e - 1] == L'\t')) e--;
      text = text.substr(0, e);
    }
    if (text.empty()) continue;
    Heading h;
    h.level = level;
    h.text = text;
    std::wstring base = slugify(text);
    if (base.empty()) base = L"section";
    int n = seen[base]++;
    h.slug = n == 0 ? base : base + L"-" + std::to_wstring(n);
    out.push_back(h);
  }
  return out;
}

std::wstring buildTocMarkdown(const std::vector<Heading>& hs, int tabSize) {
  if (hs.empty()) return std::wstring();
  int minLevel = 6;
  for (const Heading& h : hs)
    if (h.level < minLevel) minLevel = h.level;
  if (tabSize < 1) tabSize = 1;
  auto escapeLabel = [](const std::wstring& s) {  // 링크 라벨 내 [ ] 이스케이프
    std::wstring o;
    for (wchar_t c : s) {
      if (c == L'[' || c == L']') o += L'\\';
      o += c;
    }
    return o;
  };
  std::wstring out;
  for (const Heading& h : hs) {
    std::wstring indent((size_t)((h.level - minLevel) * tabSize), L' ');
    out += indent + L"- [" + escapeLabel(h.text) + L"](#" + h.slug + L")\n";
  }
  return out;
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
