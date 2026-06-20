// 파일 입출력(와이드 경로) + 경로 도우미.
#pragma once
#include <string>

// HANDLE 은 UniqueHandle 로 소유. UTF-8 BOM 은 읽기 시 제거.
bool readFile(const std::wstring& path, std::string& out);
bool writeFile(const std::wstring& path, const std::string& bytes);

std::wstring baseName(const std::wstring& p);
std::wstring exeDir();
std::wstring settingsPath();
std::wstring dirOf(const std::wstring& p);
