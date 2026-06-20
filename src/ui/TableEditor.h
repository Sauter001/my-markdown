// 표 편의 기능: EDIT 버퍼에 대한 정렬/삽입/Tab 셀 이동/Enter 행 추가.
//   순수 파싱은 core/markdown 을 쓰고, 여기서는 EDIT 좌표 추출/치환/셀 선택만
//   담당.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "core/markdown.h"

class TableEditor {
 public:
  bool formatTable(HWND edit);                         // 커서가 속한 표 재정렬
  bool insertSkeleton(HWND edit, int cols, int rows);  // 표 골격 삽입
  bool nav(HWND edit, bool shift);  // Tab/Shift+Tab 셀 이동 (표 아니면 false)
  bool enter(HWND edit);            // Enter 행 추가 (표 아니면 false)

 private:
  void replaceSel(HWND edit, DWORD a, DWORD b, const std::wstring& text);
  bool selectCell(HWND edit, const TblLine& line, int cellIdx);
  bool addColumn(HWND edit, const std::vector<TblLine>& lines, int top, int bot,
                 int ci);
};
