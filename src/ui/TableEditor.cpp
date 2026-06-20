#include "ui/TableEditor.h"

#include <algorithm>

#include "ui/edit_util.h"

void TableEditor::replaceSel(HWND edit, DWORD a, DWORD b,
                             const std::wstring& text) {
  SendMessageW(edit, EM_SETSEL, a, b);
  SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)text.c_str());  // Undo 보존
}
bool TableEditor::selectCell(HWND edit, const TblLine& line, int cellIdx) {
  std::vector<int> pipes = tblPipes(line.text);
  if (cellIdx < 0 || cellIdx > (int)pipes.size() - 2) return false;
  int cs = pipes[cellIdx] + 1, ce = pipes[cellIdx + 1];
  int s = cs;
  while (s < ce && line.text[s] == L' ') s++;
  int e = ce;
  while (e > s && line.text[e - 1] == L' ') e--;
  SendMessageW(edit, EM_SETSEL, (WPARAM)(line.start + s),
               (LPARAM)(line.start + e));
  return true;
}

// 커서가 속한 표를 셀 폭에 맞춰 재정렬(정렬 방향 유지, 구분선 없으면 생성).
bool TableEditor::formatTable(HWND edit) {
  std::wstring val = editGetTextW(edit);
  DWORD a, b;
  editGetSel(edit, a, b);
  std::vector<TblLine> lines = tblGetLines(val);
  int ci = tblLineIndexAt(lines, a);
  if (!tblIsRow(lines[ci].text)) return false;
  int top, bot;
  tblBounds(lines, ci, top, bot);
  struct Row {
    std::vector<std::wstring> cells;
    bool sep;
  };
  std::vector<Row> rows;
  int sepAt = -1;
  for (int k = top; k <= bot; k++) {
    bool sep = tblIsSep(lines[k].text);
    if (sep && sepAt < 0) sepAt = (int)rows.size();
    rows.push_back({tblParseCells(lines[k].text), sep});
  }
  int cols = 0;
  for (auto& r : rows) cols = std::max(cols, (int)r.cells.size());
  std::vector<int> align(cols, 0);  // 0 none, 1 left, 2 right, 3 center
  if (sepAt >= 0) {
    auto& sc = rows[sepAt].cells;
    for (int c = 0; c < cols; c++) {
      std::wstring x = c < (int)sc.size() ? tblTrim(sc[c]) : L"";
      bool L_ = !x.empty() && x.front() == L':',
           R_ = !x.empty() && x.back() == L':';
      align[c] = (L_ && R_) ? 3 : R_ ? 2 : L_ ? 1 : 0;
    }
  }
  std::vector<int> width(cols, 3);
  for (auto& r : rows) {
    if (r.sep) continue;
    for (int c = 0; c < cols; c++) {
      std::wstring cell = c < (int)r.cells.size() ? r.cells[c] : L"";
      width[c] = std::max(width[c], tblW(cell));
    }
  }
  std::vector<std::wstring> out;
  for (auto& r : rows) {
    std::vector<std::wstring> segs;
    if (r.sep) {
      for (int c = 0; c < cols; c++) {
        int w = width[c];
        std::wstring d;
        if (align[c] == 3)
          d = L":" + std::wstring((size_t)std::max(1, w - 2), L'-') + L":";
        else if (align[c] == 2)
          d = std::wstring((size_t)std::max(2, w - 1), L'-') + L":";
        else if (align[c] == 1)
          d = L":" + std::wstring((size_t)std::max(2, w - 1), L'-');
        else
          d = std::wstring((size_t)std::max(3, w), L'-');
        segs.push_back(d);
      }
    } else {
      for (int c = 0; c < cols; c++) {
        std::wstring cell = c < (int)r.cells.size() ? r.cells[c] : L"";
        if (align[c] == 2) {
          int pad = width[c] - tblW(cell);
          if (pad < 0) pad = 0;
          segs.push_back(std::wstring((size_t)pad, L' ') + cell);
        } else if (align[c] == 3)
          segs.push_back(tblPadC(cell, width[c]));
        else
          segs.push_back(tblPad(cell, width[c]));
      }
    }
    std::wstring ln = L"| ";
    for (size_t i = 0; i < segs.size(); i++) {
      if (i) ln += L" | ";
      ln += segs[i];
    }
    ln += L" |";
    out.push_back(ln);
  }
  if (sepAt < 0) {
    std::vector<std::wstring> segs;
    for (int c = 0; c < cols; c++)
      segs.push_back(std::wstring((size_t)std::max(3, width[c]), L'-'));
    std::wstring ln = L"| ";
    for (size_t i = 0; i < segs.size(); i++) {
      if (i) ln += L" | ";
      ln += segs[i];
    }
    ln += L" |";
    out.insert(out.begin() + 1, ln);
  }
  DWORD blockStart = lines[top].start;
  DWORD blockEnd = lines[bot].start + (DWORD)lines[bot].text.size();
  std::wstring joined;
  for (size_t i = 0; i < out.size(); i++) {
    if (i) joined += L"\r\n";
    joined += out[i];
  }
  replaceSel(edit, blockStart, blockEnd, joined);
  SendMessageW(edit, EM_SETSEL, blockStart, blockStart);
  return true;
}

// 표 골격 삽입(헤더 + 구분선 + 빈 본문). 첫 헤더 셀을 선택.
bool TableEditor::insertSkeleton(HWND edit, int cols, int rows) {
  std::wstring header = L"| ";
  for (int i = 0; i < cols; i++) {
    if (i) header += L" | ";
    header += L"제목" + std::to_wstring(i + 1);
  }
  header += L" |";
  std::wstring sep = L"| ";
  for (int i = 0; i < cols; i++) {
    if (i) sep += L" | ";
    sep += L"---";
  }
  sep += L" |";
  std::vector<std::wstring> body;
  for (int r = 0; r < rows; r++) {
    std::wstring row = L"| ";
    for (int i = 0; i < cols; i++) {
      if (i) row += L" | ";
      row += L"  ";
    }
    row += L" |";
    body.push_back(row);
  }
  DWORD pos, selEnd;
  editGetSel(edit, pos, selEnd);
  std::wstring val = editGetTextW(edit);
  bool atStart =
      (pos == 0) || (pos <= (DWORD)val.size() && val[pos - 1] == L'\n');
  std::wstring prefix = atStart ? L"" : L"\r\n";
  std::wstring text = prefix + header + L"\r\n" + sep;
  for (auto& bln : body) text += L"\r\n" + bln;
  text += L"\r\n";
  replaceSel(edit, pos, pos, text);
  DWORD firstCell = pos + (DWORD)prefix.size() + 2;  // "| " 다음
  std::wstring first = L"제목1";
  SendMessageW(edit, EM_SETSEL, (WPARAM)firstCell,
               (LPARAM)(firstCell + (DWORD)first.size()));
  SetFocus(edit);
  return true;
}

// 마지막 열에서 Tab: 모든 행에 빈 셀 추가 후(구분선 있으면 재정렬) 같은 행 새
// 셀 선택.
bool TableEditor::addColumn(HWND edit, const std::vector<TblLine>& lines,
                            int top, int bot, int ci) {
  bool hasSep = false;
  std::vector<std::wstring> out;
  for (int k = top; k <= bot; k++) {
    bool sep = tblIsSep(lines[k].text);
    if (sep) hasSep = true;
    std::wstring t = tblTrim(lines[k].text);
    if (t.empty() || t.front() != L'|') t = L"| " + t;
    if (t.empty() || t.back() != L'|') t = t + L" |";
    out.push_back(sep ? (t + L" --- |") : (t + L"   |"));
  }
  DWORD blockStart = lines[top].start;
  DWORD blockEnd = lines[bot].start + (DWORD)lines[bot].text.size();
  std::wstring joined;
  for (size_t i = 0; i < out.size(); i++) {
    if (i) joined += L"\r\n";
    joined += out[i];
  }
  replaceSel(edit, blockStart, blockEnd, joined);
  if (hasSep) {
    SendMessageW(edit, EM_SETSEL, blockStart, blockStart);
    formatTable(edit);
  }
  std::vector<TblLine> lines2 = tblGetLines(editGetTextW(edit));
  if (ci >= 0 && ci < (int)lines2.size()) {
    const TblLine& tgt = lines2[ci];
    std::vector<int> pp = tblPipes(tgt.text);
    selectCell(edit, tgt, (int)pp.size() - 2);
  }
  return true;
}

// Tab/Shift+Tab 셀 이동. 표가 아니면 false.
bool TableEditor::nav(HWND edit, bool shift) {
  std::wstring val = editGetTextW(edit);
  DWORD a, b;
  editGetSel(edit, a, b);
  std::vector<TblLine> lines = tblGetLines(val);
  int ci = tblLineIndexAt(lines, a);
  if (!tblIsRow(lines[ci].text)) return false;
  int top, bot;
  tblBounds(lines, ci, top, bot);
  const TblLine& line = lines[ci];
  std::vector<int> pipes = tblPipes(line.text);
  int col = (int)a - (int)line.start;
  if ((int)pipes.size() < 2) return false;
  int cell = -1;
  for (int k = 0; k < (int)pipes.size() - 1; k++) {
    if (col >= pipes[k] && col <= pipes[k + 1]) {
      cell = k;
      break;
    }
  }
  if (cell < 0) {
    selectCell(edit, line, 0);
    return true;
  }
  if (!shift) {
    if (cell + 1 <= (int)pipes.size() - 2) {
      selectCell(edit, line, cell + 1);
      return true;
    }
    return addColumn(edit, lines, top, bot, ci);  // 마지막 열: 열 추가
  }
  if (cell - 1 >= 0) {
    selectCell(edit, line, cell - 1);
    return true;
  }
  int pr = tblPrevRow(lines, ci - 1, top);
  if (pr >= 0) {
    const TblLine& pl = lines[pr];
    std::vector<int> pp = tblPipes(pl.text);
    selectCell(edit, pl, (int)pp.size() - 2);
    return true;
  }
  return true;
}

// 표 행에서 Enter: 같은 열 수 빈 행 추가(구분선 없으면 구분선 + 빈 행). 표가
// 아니면 false.
bool TableEditor::enter(HWND edit) {
  std::wstring val = editGetTextW(edit);
  DWORD a, b;
  editGetSel(edit, a, b);
  if (a != b) return false;
  std::vector<TblLine> lines = tblGetLines(val);
  int ci = tblLineIndexAt(lines, a);
  if (!tblIsRow(lines[ci].text)) return false;
  int top, bot;
  tblBounds(lines, ci, top, bot);
  int sepIdx = -1;
  for (int k = top; k <= bot; k++) {
    if (tblIsSep(lines[k].text)) {
      sepIdx = k;
      break;
    }
  }
  int cols = (int)tblParseCells(lines[top].text).size();
  std::wstring emptyRow = L"|";
  for (int i = 0; i < cols; i++) emptyRow += L"  |";
  DWORD insertPos, rowStart;
  std::wstring insertText;
  if (sepIdx < 0) {
    std::wstring sep = L"| ";
    for (int i = 0; i < cols; i++) {
      if (i) sep += L" | ";
      sep += L"---";
    }
    sep += L" |";
    insertPos = lines[ci].start + (DWORD)lines[ci].text.size();
    insertText = L"\r\n" + sep + L"\r\n" + emptyRow;
    rowStart = insertPos + 2 + (DWORD)sep.size() + 2;
  } else {
    int anchor = (ci < sepIdx) ? sepIdx : ci;
    insertPos = lines[anchor].start + (DWORD)lines[anchor].text.size();
    insertText = L"\r\n" + emptyRow;
    rowStart = insertPos + 2;
  }
  replaceSel(edit, insertPos, insertPos, insertText);
  DWORD caret = rowStart + 1;  // 새 행 첫 셀(| 다음)
  SendMessageW(edit, EM_SETSEL, caret, caret);
  return true;
}
