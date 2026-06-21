// 평면 JSON 최소 파서/직렬화 (settings.json 용) - 순수 함수.
#pragma once
#include <string>

std::string jsonStr(const std::string& j, const char* key,
                    const std::string& dft);
int jsonInt(const std::string& j, const char* key, int dft);
bool jsonBool(const std::string& j, const char* key, bool dft);
std::string jsonArrayRaw(const std::string& j, const char* key,
                         const std::string& dft);
std::string jsonObjectRaw(const std::string& j, const char* key,
                          const std::string& dft);
std::string jsonEscape(const std::string& s);
