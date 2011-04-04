/*
Copyright 2011 Google Inc. All Rights Reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
    * Neither the name of Google Inc. nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Various stubs for the open-source version of Snappy.

File modified for the Linux Kernel by
Zeev Tarantov <zeev.tarantov@gmail.com>
*/

#ifndef UTIL_SNAPPY_OPENSOURCE_SNAPPY_STUBS_INTERNAL_H_
#define UTIL_SNAPPY_OPENSOURCE_SNAPPY_STUBS_INTERNAL_H_

#ifdef __KERNEL__
#include <linux/stddef.h>
#include <linux/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif


/* Static prediction hints. */
#ifdef __KERNEL__
#include <linux/compiler.h>
#else
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#define noinline	__attribute__((noinline))
#endif


#ifdef DEBUG
#ifdef __KERNEL__
#define DCHECK(cond)	if (!(cond)) printk(KERN_DEBUG "assert failed @ %s:%i\n", __FILE__, __LINE__)
#else
#include <assert.h>
#define DCHECK(cond)	assert(cond)
#endif
#else
#define DCHECK(cond)
#endif

#define DCHECK_EQ(a, b)	DCHECK(((a) == (b)))
#define DCHECK_NE(a, b)	DCHECK(((a) != (b)))
#define DCHECK_GT(a, b)	DCHECK(((a) >  (b)))
#define DCHECK_GE(a, b)	DCHECK(((a) >= (b)))
#define DCHECK_LT(a, b)	DCHECK(((a) <  (b)))
#define DCHECK_LE(a, b)	DCHECK(((a) <= (b)))


/* Convert to little-endian storage, opposite of network format. */
#ifdef __KERNEL__
#include <asm/byteorder.h>
#else
#if defined(__BIG_ENDIAN) && !defined(__LITTLE_ENDIAN)

/* The following guarantees declaration of the byte swap functions. */
#ifdef _MSC_VER
#include <stdlib.h>
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#elif defined(__APPLE__)
/* Mac OS X / Darwin features */
#include <libkern/OSByteOrder.h>
#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)
#else
#include <byteswap.h>
#endif

static inline uint16_t cpu_to_le16(uint32_t v) { return bswap_16(v); }
static inline uint16_t le32_to_cpu(uint16_t v) { return bswap_16(v); }
static inline uint32_t cpu_to_le32(uint32_t v) { return bswap_32(v); }
static inline uint32_t le32_to_cpu(uint32_t v) { return bswap_32(v); }

#else /* !defined(__BIG_ENDIAN) */
#define cpu_to_le16(x) (x)
#define le16_to_cpu(x) (x)
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) (x)
#endif /* !defined(__BIG_ENDIAN) */
#endif /* !defined(__KERNEL__) */
#if !defined(__LITTLE_ENDIAN) && !defined(__BIG_ENDIAN)
# error __LITTLE_ENDIAN or __BIG_ENDIAN has to be defined
#endif


/* Potentially unaligned loads and stores. */

#ifdef __KERNEL__
#include <asm/unaligned.h>

#define UNALIGNED_LOAD16(_p)		get_unaligned((const uint16_t*)(_p))
#define UNALIGNED_LOAD32(_p)		get_unaligned((const uint32_t*)(_p))
#define UNALIGNED_LOAD64(_p)		get_unaligned((const uint64_t*)(_p))

#define UNALIGNED_STORE16(_p, _val)	put_unaligned((_val), (uint16_t*)(_p))
#define UNALIGNED_STORE32(_p, _val)	put_unaligned((_val), (uint32_t*)(_p))
#define UNALIGNED_STORE64(_p, _val)	put_unaligned((_val), (uint64_t*)(_p))

#else /* !defined(__KERNEL__) */

#if defined(__i386__) || defined(__x86_64__) || defined(__powerpc__)

#define UNALIGNED_LOAD16(_p) (*(const uint16_t*)(_p))
#define UNALIGNED_LOAD32(_p) (*(const uint32_t*)(_p))
#define UNALIGNED_LOAD64(_p) (*(const uint64_t*)(_p))

#define UNALIGNED_STORE16(_p, _val) (*(uint16_t*)(_p) = (_val))
#define UNALIGNED_STORE32(_p, _val) (*(uint32_t*)(_p) = (_val))
#define UNALIGNED_STORE64(_p, _val) (*(uint64_t*)(_p) = (_val))

#else /* !(x86 || powerpc) */

/* These functions are provided for architectures that don't support
   unaligned loads and stores. */

static inline uint16_t UNALIGNED_LOAD16(const void *p)
{
  uint16_t t;
  memcpy(&t, p, sizeof t);
  return t;
}

static inline uint32_t UNALIGNED_LOAD32(const void *p)
{
  uint32_t t;
  memcpy(&t, p, sizeof t);
  return t;
}

static inline uint64_t UNALIGNED_LOAD64(const void *p)
{
  uint64_t t;
  memcpy(&t, p, sizeof t);
  return t;
}

static inline void UNALIGNED_STORE16(void *p, uint16_t v)
{
  memcpy(p, &v, sizeof v);
}

static inline void UNALIGNED_STORE32(void *p, uint32_t v)
{
  memcpy(p, &v, sizeof v);
}

static inline void UNALIGNED_STORE64(void *p, uint64_t v)
{
  memcpy(p, &v, sizeof v);
}

#endif /* !(x86 || powerpc) */
#endif /* !defined(__KERNEL__) */


enum {
  LITERAL = 0,
  COPY_1_BYTE_OFFSET = 1,  /* 3 bit length + 3 bits of offset in opcode */
  COPY_2_BYTE_OFFSET = 2,
  COPY_4_BYTE_OFFSET = 3
};

#endif  /* UTIL_SNAPPY_OPENSOURCE_SNAPPY_STUBS_INTERNAL_H_ */
