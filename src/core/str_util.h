// 문자열/인코딩 순수 유틸 (UTF-8 <-> UTF-16, base64, file URL, 줄바꿈 정규화).
#pragma once
#include <string>

// UTF-8 <-> UTF-16
std::wstring utf8_to_wide(const std::string& s);
std::string wide_to_utf8(const std::wstring& w);
// 소스의 UTF-8 좁은 리터럴을 와이드로 (한글 UI 문자열용)
std::wstring W(const char* utf8);

// base64 (네이티브 <-> 프리뷰 브리지용)
std::string base64_encode(const std::string& in);
std::string base64_decode(const std::string& in);
// 브리지 인자(base64 문자열)에서 첫 따옴표 문자열 추출
std::string firstStringArg(const std::string& req);
// file:/// URL 생성 (UTF-8 퍼센트 인코딩)
std::string toFileUrl(const std::wstring& path);

// 줄바꿈 정규화 (EDIT 는 CRLF, 파일은 LF 로 유지)
std::string crlfToLF(const std::string& s);
std::string lfToCRLF(const std::string& s);
