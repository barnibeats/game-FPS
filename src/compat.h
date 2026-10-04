#pragma once
// MinGW lacks the array-size template overloads of the *_s string functions that MSVC provides.
#include <windows.h>
#include <cwchar>

#if defined(__MINGW32__)
template <size_t N, class... Args>
inline int swprintf_s(wchar_t (&buf)[N], const wchar_t* fmt, Args... args) {
    return swprintf_s(buf, N, fmt, args...);
}
#endif
