/*
 * UniXNU Freestanding Type Definitions
 *
 * Copyright (c) 2026 UniXNU Project
 *
 * Used in place of <stdint.h> / <stdbool.h> in freestanding code
 * where -nostdinc prevents using the compiler's built-in headers.
 */

#ifndef _UNIXNU_TYPES_H_
#define _UNIXNU_TYPES_H_

/* Exact-width unsigned integers */
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

/* Exact-width signed integers */
typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef signed long long    int64_t;

/* Size type */
typedef unsigned long long  size_t;
typedef long long           ssize_t;
typedef long long           ptrdiff_t;

/* Boolean — C23 onwards has bool as keyword, older C needs stdbool.h */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
/* C23+: bool, true, false are keywords, no definition needed */
#else
/* Pre-C23: use stdbool semantics */
#ifndef __cplusplus
typedef int                 bool;
#define true                1
#define false               0
#endif
#endif

/* NULL */
#ifndef NULL
#define NULL                ((void *)0)
#endif

/* Limits */
#define UINT8_MAX           0xFFU
#define UINT16_MAX          0xFFFFU
#define UINT32_MAX          0xFFFFFFFFU
#define UINT64_MAX          0xFFFFFFFFFFFFFFFFULL

/* Helpful macros */
#define ARRAY_SIZE(a)       (sizeof(a) / sizeof((a)[0]))

/* Compiler hints */
#define __packed            __attribute__((packed))
#define __aligned(x)        __attribute__((aligned(x)))
#define __unused            __attribute__((unused))
#define __weak              __attribute__((weak))

#endif /* _UNIXNU_TYPES_H_ */
