// 마크다운 리스트/표 순수 파싱 로직 (Win32 컨트롤 비의존, 단위 테스트 가능).
//   표 좌표는 EDIT 버퍼 오프셋과 동일한 의미라 DWORD 를 쓴다.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

// --- 공통 ---
std::wstring rtrimWs(const std::wstring& s);

// --- 리스트 ---
struct ListItem {
  std::wstring indent;
  bool ordered = false;
  wchar_t delim = L'.';
  int num = 0;
  std::wstring rest;  // ordered 재구성용 (마커 뒤 내용)
};
bool parseListItem(const std::wstring& line, ListItem& it);
int leadingWsLen(const std::wstring& s);
// 새 들여쓰기 레벨에서 ordered 시작 번호 (이전 형제가 ordered면 +1, 아니면 1)
int orderedStartNum(const std::vector<std::wstring>& lines, int curIdx,
                    int newIndentLen);
std::vector<std::wstring> splitLines(const std::wstring& text);

// --- 헤더 / 목차(TOC) ---
struct Heading {
  int level = 1;       // 1-6 (# 개수)
  std::wstring text;   // 헤더 raw 텍스트(# 와 닫는 # 제거, 트림)
  std::wstring slug;   // anchor id (중복 시 -1,-2 ... 부여됨)
};
// 헤더 raw 텍스트 -> anchor slug (중복 처리 제외한 base). 미리보기(web/preview.js
// 의 slugify)와 규칙이 반드시 일치해야 한다(같은 raw 입력에 같은 결과).
std::wstring slugify(const std::wstring& raw);
// 문서에서 ATX 헤더(#~######)를 순서대로 추출. 펜스 코드블록 내부는 제외.
// slug 는 등장 순서 중복 처리까지 반영해 채운다.
std::vector<Heading> parseHeadings(const std::wstring& docText);
// 추출된 헤더로 중첩 마크다운 링크 목록을 만든다(헤더 없으면 빈 문자열).
// 줄 결합은 \n (RichEdit 줄바꿈=1문자 불변식).
std::wstring buildTocMarkdown(const std::vector<Heading>& hs, int tabSize);

// --- 표 ---
//   TblLine.text 는 \r 를 뺀 줄 내용, TblLine.start 는 버퍼 절대 오프셋.
struct TblLine {
  std::wstring text;
  DWORD start;
};

std::wstring tblTrim(const std::wstring& s);
std::vector<TblLine> tblGetLines(const std::wstring& val);
int tblLineIndexAt(const std::vector<TblLine>& lines, DWORD pos);
bool tblIsRow(const std::wstring& text);
std::vector<std::wstring> tblParseCells(const std::wstring& line);
bool tblIsSep(const std::wstring& text);
std::vector<int> tblPipes(const std::wstring& text);
void tblBounds(const std::vector<TblLine>& lines, int idx, int& top, int& bot);
int tblW(const std::wstring& s);  // 전각 폭 2 반영한 표시 폭
std::wstring tblPad(const std::wstring& s, int n);
std::wstring tblPadC(const std::wstring& s, int n);
int tblPrevRow(const std::vector<TblLine>& lines, int from, int top);
