# MyMD에 오신 것을 환영합니다

간이 마크다운 에디터입니다. 좌측에서 편집하고 우측에서 즉시 렌더링을 확인하세요.

## 기본 문법
**굵게**, *기울임*, ~~취소선~~, `인라인 코드`, [링크](https://example.com)

- 목록 항목 1
- 목록 항목 2
  - 중첩 항목

1. 리스트 항목 1
2. 리스트 항목 2
    1. 항목 2.1
    2. 항목 2.2

> 인용문입니다.

## 코드 블록
```python
def hello():
    print("Hello, MyMD")
```

```c
#include <stdio.h>

int main(int argc, char* argv) {
    printf("Hello World!\n");
    return 0;
}
```

```cpp
#include <iostream>

int main(void) {
    std::cout << "Hello World" << std::endl;
    return 0;
}
```

## 수식 (LaTeX)
인라인: $E = mc^2$, 그리고 $\int_0^1 x^2\,dx = \tfrac{1}{3}$

$$
N(\mu, \sigma^{2}) = \frac{1}{\sqrt{2\pi}\,\sigma} e^{-\frac{(x-\mu)^2}{2\sigma^2}}
$$

## 토글 (details)
<details>
<summary>여기를 클릭해 펼치기</summary>

<div> 
숨겨진 내용입니다. 토글 버튼이 동작합니다.
</div>

</details>

## 이미지
![MyMD 로고](data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxNjAiIGhlaWdodD0iNDgiPjxyZWN0IHdpZHRoPSIxNjAiIGhlaWdodD0iNDgiIHJ4PSI4IiBmaWxsPSIjMDk2OWRhIi8+PHRleHQgeD0iODAiIHk9IjMwIiBmaWxsPSIjZmZmZmZmIiBmb250LWZhbWlseT0ic2Fucy1zZXJpZiIgZm9udC1zaXplPSIxOCIgdGV4dC1hbmNob3I9Im1pZGRsZSI+TXlNRDwvdGV4dD48L3N2Zz4=)

## 표
| 기능 | 지원 |
| --- | --- |
| 마크다운 | O |
| 수식 | O |
| 토글 | O |

## 작업 목록
- [x] 에디터/미리보기 병치
- [x] 수식 렌더링
- [ ] 추가 기능
