// [I] gd01: the MSVC-only helpers the FidelityFX SDK 1.1.4 sources use, for
// gcc / clang / mingw (force-included into the SDK's translation units only).
#pragma once
#ifdef __cplusplus
#include <bit>
#include <cmath>
#include <math.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <locale>
#include <codecvt>
#include <string>

#if !defined(_MSC_VER) && !defined(__MINGW32__)  // mingw-w64 has the secure CRT functions
#ifndef _countof
template <typename T, size_t N>
constexpr size_t ffxPortCountof(T (&)[N]) { return N; }
#define _countof(a) ffxPortCountof(a)
#endif
inline int wcscpy_s(wchar_t* dst, size_t n, const wchar_t* src) {
    if (!dst || !n) return 1;
    wcsncpy(dst, src ? src : L"", n - 1);
    dst[n - 1] = 0;
    return 0;
}
template <size_t N>
inline int wcscpy_s(wchar_t (&dst)[N], const wchar_t* src) { return wcscpy_s(dst, N, src); }
inline int strcpy_s(char* dst, size_t n, const char* src) {
    if (!dst || !n) return 1;
    strncpy(dst, src ? src : "", n - 1);
    dst[n - 1] = 0;
    return 0;
}
template <size_t N>
inline int strcpy_s(char (&dst)[N], const char* src) { return strcpy_s(dst, N, src); }
#define sprintf_s snprintf
#define swprintf_s swprintf
#endif  // !_MSC_VER && !__MINGW32__
#ifndef FFX_UNUSED
#define FFX_UNUSED(x) ((void)(x))
#endif
#endif  // __cplusplus
