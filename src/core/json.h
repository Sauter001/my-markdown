// 평면 JSON 최소 파서/직렬화 (settings.json 용) - 순수 함수.
#pragma once
#include <string>
#include <vector>

std::string jsonStr(const std::string& j, const char* key,
                    const std::string& dft);
int jsonInt(const std::string& j, const char* key, int dft);
bool jsonBool(const std::string& j, const char* key, bool dft);
std::string jsonArrayRaw(const std::string& j, const char* key,
                         const std::string& dft);
std::string jsonObjectRaw(const std::string& j, const char* key,
                          const std::string& dft);
std::string jsonEscape(const std::string& s);

// 문자열 배열을 벡터로 파싱. 키가 없거나 배열이 아니면 dft 반환.
std::vector<std::string> jsonStringArray(const std::string& j, const char* key,
                                         const std::vector<std::string>& dft);
// 문자열 벡터를 ["a","b"] 형태 JSON 배열 리터럴로 직렬화(빈 벡터는 []).
std::string jsonArray(const std::vector<std::string>& items);
