#ifndef _COMMON_H_
#define _COMMON_H_
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <memory.h>
#include <stdio.h>

#define BIT_DEPTH 8

#define FF_PROFILE_HEVC_MAIN                        1
#define FF_PROFILE_HEVC_MAIN_10                     2
#define FF_PROFILE_HEVC_MAIN_STILL_PICTURE          3
#define FF_PROFILE_HEVC_REXT                        4

#if defined(__INTEL_COMPILER) && __INTEL_COMPILER < 1110 || defined(__SUNPRO_C)
#   define DECLARE_ALIGNED(n,t,v)      t       __attribute__ ((aligned (n))) v
#   define DECLARE_ASM_CONST(n,t,v)    const t __attribute__ ((aligned (n))) v
#elif defined(__TI_COMPILER_VERSION__)
#   define DECLARE_ALIGNED(n,t,v)                      \
            AV_PRAGMA(DATA_ALIGN(v,n))                      \
            t __attribute__((aligned(n))) v
#   define DECLARE_ASM_CONST(n,t,v)                    \
            AV_PRAGMA(DATA_ALIGN(v,n))                      \
            static const t __attribute__((aligned(n))) v
#elif defined(__GNUC__) || defined(__clang__)
#   define DECLARE_ALIGNED(n,t,v)      t __attribute__ ((aligned (n))) v
#   define DECLARE_ASM_CONST(n,t,v)    static const t av_used __attribute__ ((aligned (n))) v
#elif defined(_MSC_VER)
#   define DECLARE_ALIGNED(n,t,v)      __declspec(align(n)) t v
#   define DECLARE_ASM_CONST(n,t,v)    __declspec(align(n)) static const t v
#else
#   define DECLARE_ALIGNED(n,t,v)      t v
#   define DECLARE_ASM_CONST(n,t,v)    static const t v
#endif


#ifdef __GNUC__
#    define AV_GCC_VERSION_AT_LEAST(x,y) (__GNUC__ > (x) || __GNUC__ == (x) && __GNUC_MINOR__ >= (y))
#    define AV_GCC_VERSION_AT_MOST(x,y)  (__GNUC__ < (x) || __GNUC__ == (x) && __GNUC_MINOR__ <= (y))
#else
#    define AV_GCC_VERSION_AT_LEAST(x,y) 0
#    define AV_GCC_VERSION_AT_MOST(x,y)  0
#endif

#ifndef av_always_inline
#if AV_GCC_VERSION_AT_LEAST(3,1)
#    define av_always_inline __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#    define av_always_inline __forceinline
#else
#    define av_always_inline inline
#endif
#endif

#ifndef av_extern_inline
#if defined(__ICL) && __ICL >= 1210 || defined(__GNUC_STDC_INLINE__)
#    define av_extern_inline extern inline
#else
#    define av_extern_inline inline
#endif
#endif

#if AV_GCC_VERSION_AT_LEAST(3,4)
#    define av_warn_unused_result __attribute__((warn_unused_result))
#else
#    define av_warn_unused_result
#endif

#if AV_GCC_VERSION_AT_LEAST(3,1)
#    define av_noinline __attribute__((noinline))
#elif defined(_MSC_VER)
#    define av_noinline __declspec(noinline)
#else
#    define av_noinline
#endif

#if AV_GCC_VERSION_AT_LEAST(3,1)
#    define av_pure __attribute__((pure))
#else
#    define av_pure
#endif

#if AV_GCC_VERSION_AT_LEAST(2,6)
#    define av_const __attribute__((const))
#else
#    define av_const
#endif

#if AV_GCC_VERSION_AT_LEAST(4,3)
#    define av_cold __attribute__((cold))
#else
#    define av_cold
#endif

#if AV_GCC_VERSION_AT_LEAST(4,1) && !defined(__llvm__)
#    define av_flatten __attribute__((flatten))
#else
#    define av_flatten
#endif

#if AV_GCC_VERSION_AT_LEAST(3,1)
#    define attribute_deprecated __attribute__((deprecated))
#elif defined(_MSC_VER)
#    define attribute_deprecated __declspec(deprecated)
#else
#    define attribute_deprecated
#endif

/**
 * Disable warnings about deprecated features
 * This is useful for sections of code kept for backward compatibility and
 * scheduled for removal.
 */
#ifndef AV_NOWARN_DEPRECATED
#if AV_GCC_VERSION_AT_LEAST(4,6)
#    define AV_NOWARN_DEPRECATED(code) \
        _Pragma("GCC diagnostic push") \
        _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"") \
        code \
        _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
#    define AV_NOWARN_DEPRECATED(code) \
        __pragma(warning(push)) \
        __pragma(warning(disable : 4996)) \
        code; \
        __pragma(warning(pop))
#else
#    define AV_NOWARN_DEPRECATED(code) code
#endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#    define av_unused __attribute__((unused))
#else
#    define av_unused
#endif

/**
 * Mark a variable as used and prevent the compiler from optimizing it
 * away.  This is useful for variables accessed only from inline
 * assembler without the compiler being aware.
 */
#if AV_GCC_VERSION_AT_LEAST(3,1) || defined(__clang__)
#    define av_used __attribute__((used))
#else
#    define av_used
#endif

#if AV_GCC_VERSION_AT_LEAST(3,3)
#   define av_alias __attribute__((may_alias))
#else
#   define av_alias
#endif

#if defined(__GNUC__) && !defined(__INTEL_COMPILER) && !defined(__clang__)
#    define av_uninit(x) x=x
#else
#    define av_uninit(x) x
#endif

#ifdef __GNUC__
#    define av_builtin_constant_p __builtin_constant_p
#    define av_printf_format(fmtpos, attrpos) __attribute__((__format__(__printf__, fmtpos, attrpos)))
#else
#    define av_builtin_constant_p(x) 0
#    define av_printf_format(fmtpos, attrpos)
#endif

#if AV_GCC_VERSION_AT_LEAST(2,5)
#    define av_noreturn __attribute__((noreturn))
#else
#    define av_noreturn
#endif

#define FFMAX(a, b) ((a) > (b) ? (a) : (b))
#define FFMAX3(a, b, c) FFMAX(FFMAX(a, b), c)
#define FFMIN(a, b) ((a) > (b) ? (b) : (a))
#define FFMIN3(a, b, c) FFMIN(FFMIN(a, b), c)

static av_always_inline av_const int32_t av_clipl_int32_c(int64_t a)
{
    if ((a+0x80000000u) & ~UINT64_C(0xFFFFFFFF)) return (int32_t)((a>>63) ^ 0x7FFFFFFF);
    else                                         return (int32_t)a;
}

static av_always_inline av_const int av_clip32_c(int a, int amin, int amax)
{
    if      (a < amin) return amin;
    else if (a > amax) return amax;
    else               return a;
}


static av_always_inline av_const int64_t av_clip64_c(int64_t a, int64_t amin, int64_t amax)
{
    if      (a < amin) return amin;
    else if (a > amax) return amax;
    else               return a;
}


static av_always_inline void *fd_malloc(uint64_t size)
{
    if(size > INT32_MAX - 32 || size <= 0)
        return NULL;
    char *ptr = (char *)malloc(size + 32);
    if(!ptr)
        return NULL;
    char *p = ptr + 32;
    p = (char *)((uintptr_t)p & (~0x1F));
    char diff = (char)(p - ptr);
    p[-1] = diff;
    return p;
}

static av_always_inline void fd_free(void *ptr)
{
    if(ptr)
        free((char *)ptr - ((char *)ptr)[-1]);
}

static av_always_inline void fd_mallocp(void **ptr, uint64_t size)
{
    if(*ptr)
        return;
    *ptr = fd_malloc(size);
}

static av_always_inline void fd_freep(void **ptr)
{
    if(!*ptr)
        return;
    fd_free(*ptr);
    *ptr = NULL;
}

static av_always_inline void fd_fast_realloc(void **ptr, uint64_t *oldsize, uint64_t newsize)
{
    if(newsize <= *oldsize)
        return;
    fd_freep(ptr);
    *ptr = fd_malloc(newsize);
    if(!*ptr)
        newsize = 0;
    *oldsize = newsize;
}

#endif