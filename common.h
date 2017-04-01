#ifndef _COMMON_H_
#define _COMMON_H_
#include <stdint.h>

#if defined(__INTEL_COMPILER) && __INTEL_COMPILER < 1110 || defined(__SUNPRO_C)

#define DECLARE_ALIGNED(n,t,v)      t       __attribute__ ((aligned (n))) v
#define DECLARE_ASM_CONST(n,t,v)    const t __attribute__ ((aligned (n))) v

#elif defined(__TI_COMPILER_VERSION__)
#define DECLARE_ALIGNED(n,t,v)                      \
        AV_PRAGMA(DATA_ALIGN(v,n))                      \
        t __attribute__((aligned(n))) v
#define DECLARE_ASM_CONST(n,t,v)                    \
        AV_PRAGMA(DATA_ALIGN(v,n))                      \
        static const t __attribute__((aligned(n))) v
#elif defined(__GNUC__) || defined(__clang__)
#define DECLARE_ALIGNED(n,t,v)      t __attribute__ ((aligned (n))) v
#define DECLARE_ASM_CONST(n,t,v)    static const t av_used __attribute__ ((aligned (n))) v
#elif defined(_MSC_VER)
#define DECLARE_ALIGNED(n,t,v)      __declspec(align(n)) t v
    #define DECLARE_ASM_CONST(n,t,v)    __declspec(align(n)) static const t v
#else
    #define DECLARE_ALIGNED(n,t,v)      t v
    #define DECLARE_ASM_CONST(n,t,v)    static const t v
#endif

#endif