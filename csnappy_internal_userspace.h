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

#ifndef SNAPPY_INTERNAL_USERSPACE_H_
#define SNAPPY_INTERNAL_USERSPACE_H_

#if defined(_MSC_VER) && (_MSC_VER <= 1300)
typedef unsigned __int8  uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#elif defined(__linux__) || defined(__GLIBC__) || defined(__WIN32__) || defined(__APPLE__)
#include <stdint.h>
#else
#include <sys/types.h>
#endif
#include <string.h>

#define min(x, y) (__extension__ ({		\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; }))

/* Static prediction hints. */
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#define noinline	__attribute__((noinline))


#ifdef DEBUG
#include <assert.h>
#define DCHECK(cond)	assert(cond)
#else
#define DCHECK(cond)
#endif

/* kernel defined either one or the other, stdlib defines both */
#if __BYTE_ORDER__ == 1234
# undef __BIG_ENDIAN
#elif __BYTE_ORDER__ == 4321
# undef __LITTLE_ENDIAN
#endif

#if !defined(__LITTLE_ENDIAN) && !defined(__BIG_ENDIAN)
# error __LITTLE_ENDIAN or __BIG_ENDIAN must be defined
#endif
#if defined(__LITTLE_ENDIAN) && defined(__BIG_ENDIAN)
# error __LITTLE_ENDIAN or __BIG_ENDIAN must be defined, not both!
#endif

/* Convert to little-endian storage, opposite of network format. */
#if defined(__BIG_ENDIAN)

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
static inline uint16_t le16_to_cpu(uint16_t v) { return bswap_16(v); }
static inline uint32_t cpu_to_le32(uint32_t v) { return bswap_32(v); }
static inline uint32_t le32_to_cpu(uint32_t v) { return bswap_32(v); }

#else /* !defined(__BIG_ENDIAN) */
#define cpu_to_le16(x) (x)
#define le16_to_cpu(x) (x)
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) (x)
#endif /* !defined(__BIG_ENDIAN) */


/* Potentially unaligned loads and stores. */

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

#endif  /* SNAPPY_INTERNAL_USERSPACE_H_ */
