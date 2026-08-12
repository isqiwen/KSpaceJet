#pragma once

#include <cstdint>

#if defined(__clang__) || defined(__GNUC__)
#define KSJ_LIKELY(x) __builtin_expect(!!(x), 1)
#define KSJ_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define KSJ_LIKELY(x) (x)
#define KSJ_UNLIKELY(x) (x)
#endif

#if defined(_MSC_VER)
#define KSJ_FORCE_INLINE __forceinline
#else
#define KSJ_FORCE_INLINE inline __attribute__((always_inline))
#endif

#if defined(__cplusplus)
#define KSJ_EXTERN_C extern "C"
#else
#define KSJ_EXTERN_C extern
#endif

#if defined(_WIN32)
#define KSJ_SYMBOL_EXPORT __declspec(dllexport)
#define KSJ_SYMBOL_IMPORT __declspec(dllimport)
#define KSJ_CDECL __cdecl
#elif defined(__clang__) || defined(__GNUC__)
#define KSJ_SYMBOL_EXPORT __attribute__((visibility("default")))
#define KSJ_SYMBOL_IMPORT
#define KSJ_CDECL
#else
#define KSJ_SYMBOL_EXPORT
#define KSJ_SYMBOL_IMPORT
#define KSJ_CDECL
#endif

#define KSJ_C_EXPORT KSJ_EXTERN_C KSJ_SYMBOL_EXPORT
#define KSJ_C_IMPORT KSJ_EXTERN_C KSJ_SYMBOL_IMPORT
#define KSJ_C_API KSJ_EXTERN_C

#if defined(_MSC_VER)
#define KSJ_OMP_PARALLEL_FOR __pragma(omp parallel for)
#define KSJ_OMP_PARALLEL_FOR_COLLAPSE_2 __pragma(omp parallel for)
#define KSJ_OMP_PARALLEL_FOR_COLLAPSE_3 __pragma(omp parallel for)
#else
#define KSJ_OMP_PARALLEL_FOR _Pragma("omp parallel for")
#define KSJ_OMP_PARALLEL_FOR_COLLAPSE_2 _Pragma("omp parallel for collapse(2)")
#define KSJ_OMP_PARALLEL_FOR_COLLAPSE_3 _Pragma("omp parallel for collapse(3)")
#endif
