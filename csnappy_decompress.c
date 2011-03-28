/*
Copyright 2011, Google Inc.
All rights reserved.

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

File modified for the Linux Kernel by
Zeev Tarantov <zeev.tarantov@gmail.com>
*/

#include "csnappy_internal.h"
#include "csnappy.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#else
#include <stdlib.h>
#include <string.h>
#endif


/* Mapping from i in range [0,4] to a mask to extract the bottom 8*i bits */
static const uint32_t wordmask[] = {
  0u, 0xffu, 0xffffu, 0xffffffu, 0xffffffffu
};

/*
 * Data stored per entry in lookup table:
 *      Range   Bits-used       Description
 *      ------------------------------------
 *      1..64   0..7            Literal/copy length encoded in opcode byte
 *      0..7    8..10           Copy offset encoded in opcode byte / 256
 *      0..4    11..13          Extra bytes after opcode
 *
 * We use eight bits for the length even though 7 would have sufficed
 * because of efficiency reasons:
 *      (1) Extracting a byte is faster than a bit-field
 *      (2) It properly aligns copy offset so we do not need a <<8
 */
static const uint16_t char_table[256] = {
  0x0001, 0x0804, 0x1001, 0x2001, 0x0002, 0x0805, 0x1002, 0x2002,
  0x0003, 0x0806, 0x1003, 0x2003, 0x0004, 0x0807, 0x1004, 0x2004,
  0x0005, 0x0808, 0x1005, 0x2005, 0x0006, 0x0809, 0x1006, 0x2006,
  0x0007, 0x080a, 0x1007, 0x2007, 0x0008, 0x080b, 0x1008, 0x2008,
  0x0009, 0x0904, 0x1009, 0x2009, 0x000a, 0x0905, 0x100a, 0x200a,
  0x000b, 0x0906, 0x100b, 0x200b, 0x000c, 0x0907, 0x100c, 0x200c,
  0x000d, 0x0908, 0x100d, 0x200d, 0x000e, 0x0909, 0x100e, 0x200e,
  0x000f, 0x090a, 0x100f, 0x200f, 0x0010, 0x090b, 0x1010, 0x2010,
  0x0011, 0x0a04, 0x1011, 0x2011, 0x0012, 0x0a05, 0x1012, 0x2012,
  0x0013, 0x0a06, 0x1013, 0x2013, 0x0014, 0x0a07, 0x1014, 0x2014,
  0x0015, 0x0a08, 0x1015, 0x2015, 0x0016, 0x0a09, 0x1016, 0x2016,
  0x0017, 0x0a0a, 0x1017, 0x2017, 0x0018, 0x0a0b, 0x1018, 0x2018,
  0x0019, 0x0b04, 0x1019, 0x2019, 0x001a, 0x0b05, 0x101a, 0x201a,
  0x001b, 0x0b06, 0x101b, 0x201b, 0x001c, 0x0b07, 0x101c, 0x201c,
  0x001d, 0x0b08, 0x101d, 0x201d, 0x001e, 0x0b09, 0x101e, 0x201e,
  0x001f, 0x0b0a, 0x101f, 0x201f, 0x0020, 0x0b0b, 0x1020, 0x2020,
  0x0021, 0x0c04, 0x1021, 0x2021, 0x0022, 0x0c05, 0x1022, 0x2022,
  0x0023, 0x0c06, 0x1023, 0x2023, 0x0024, 0x0c07, 0x1024, 0x2024,
  0x0025, 0x0c08, 0x1025, 0x2025, 0x0026, 0x0c09, 0x1026, 0x2026,
  0x0027, 0x0c0a, 0x1027, 0x2027, 0x0028, 0x0c0b, 0x1028, 0x2028,
  0x0029, 0x0d04, 0x1029, 0x2029, 0x002a, 0x0d05, 0x102a, 0x202a,
  0x002b, 0x0d06, 0x102b, 0x202b, 0x002c, 0x0d07, 0x102c, 0x202c,
  0x002d, 0x0d08, 0x102d, 0x202d, 0x002e, 0x0d09, 0x102e, 0x202e,
  0x002f, 0x0d0a, 0x102f, 0x202f, 0x0030, 0x0d0b, 0x1030, 0x2030,
  0x0031, 0x0e04, 0x1031, 0x2031, 0x0032, 0x0e05, 0x1032, 0x2032,
  0x0033, 0x0e06, 0x1033, 0x2033, 0x0034, 0x0e07, 0x1034, 0x2034,
  0x0035, 0x0e08, 0x1035, 0x2035, 0x0036, 0x0e09, 0x1036, 0x2036,
  0x0037, 0x0e0a, 0x1037, 0x2037, 0x0038, 0x0e0b, 0x1038, 0x2038,
  0x0039, 0x0f04, 0x1039, 0x2039, 0x003a, 0x0f05, 0x103a, 0x203a,
  0x003b, 0x0f06, 0x103b, 0x203b, 0x003c, 0x0f07, 0x103c, 0x203c,
  0x0801, 0x0f08, 0x103d, 0x203d, 0x1001, 0x0f09, 0x103e, 0x203e,
  0x1801, 0x0f0a, 0x103f, 0x203f, 0x2001, 0x0f0b, 0x1040, 0x2040
};


static inline const char*
varint_parse32(const char *p, const char *l, uint32_t *OUTPUT)
{
	const uint8_t *ptr = (const uint8_t*)p;
	const uint8_t *limit = (const uint8_t*)l;
	uint32_t b, result;
	if (ptr >= limit) return NULL;
	b = *(ptr++);
	result = b & 127;
	if (b < 128) goto done;
	if (ptr >= limit) return NULL;
	b = *(ptr++);
	result |= (b & 127) << 7;
	if (b < 128) goto done;
	if (ptr >= limit) return NULL;
	b = *(ptr++);
	result |= (b & 127) << 14;
	if (b < 128) goto done;
	if (ptr >= limit) return NULL;
	b = *(ptr++);
	result |= (b & 127) << 21;
	if (b < 128) goto done;
	if (ptr >= limit) return NULL;
	b = *(ptr++);
	result |= (b & 127) << 28;
	if (b < 16) goto done;
	return NULL; /* Value is too long to be a varint32 */
done:
	*OUTPUT = result;
	return (const char*)ptr;
}

/*
 * Copy "len" bytes from "src" to "op", one byte at a time.  Used for
 * handling COPY operations where the input and output regions may
 * overlap.  For example, suppose:
 *    src    == "ab"
 *    op     == src + 2
 *    len    == 20
 * After IncrementalCopy(src, op, len), the result will have
 * eleven copies of "ab"
 *    ababababababababababab
 * Note that this does not match the semantics of either memcpy()
 * or memmove().
 */
static inline void IncrementalCopy(const char *src, char *op, int len)
{
	DCHECK_GT(len, 0);
	do {
		*op++ = *src++;
	} while (--len > 0);
}

/*
 * Equivalent to IncrementalCopy except that it can write up to ten extra
 * bytes after the end of the copy, and that it is faster.
 *
 * The main part of this loop is a simple copy of eight bytes at a time until
 * we've copied (at least) the requested amount of bytes.  However, if op and
 * src are less than eight bytes apart (indicating a repeating pattern of
 * length < 8), we first need to expand the pattern in order to get the correct
 * results. For instance, if the buffer looks like this, with the eight-byte
 * <src> and <op> patterns marked as intervals:
 *
 *    abxxxxxxxxxxxx
 *    [------]           src
 *      [------]         op
 *
 * a single eight-byte copy from <src> to <op> will repeat the pattern once,
 * after which we can move <op> two bytes without moving <src>:
 *
 *    ababxxxxxxxxxx
 *    [------]           src
 *        [------]       op
 *
 * and repeat the exercise until the two no longer overlap.
 *
 * This allows us to do very well in the special case of one single byte
 * repeated many times, without taking a big hit for more general cases.
 *
 * The worst case of extra writing past the end of the match occurs when
 * op - src == 1 and len == 1; the last copy will read from byte positions
 * [0..7] and write to [4..11], whereas it was only supposed to write to
 * position 1. Thus, ten excess bytes.
 */
static const int kMaxIncrementCopyOverflow = 10;
static inline void IncrementalCopyFastPath(const char *src, char *op, int len)
{
	while (op - src < 8) {
		UNALIGNED_STORE64(op, UNALIGNED_LOAD64(src));
		len -= op - src;
		op += op - src;
	}
	while (len > 0) {
		UNALIGNED_STORE64(op, UNALIGNED_LOAD64(src));
		src += 8;
		op += 8;
		len -= 8;
	}
}


/* A type that writes to a flat array. */
struct SnappyArrayWriter {
	char* base;
	char* op;
	char* op_limit;
};

static inline void
SAW__init(struct SnappyArrayWriter *this, char *dst)
{
	this->base = dst;
	this->op = dst;
}

static inline void
SAW__SetExpectedLength(struct SnappyArrayWriter *this, size_t len)
{
	this->op_limit = this->op + len;
}

static inline int
SAW__CheckLength(const struct SnappyArrayWriter *this)
{
	return this->op == this->op_limit;
}

static inline int
SAW__Append(struct SnappyArrayWriter *this,
	    const char* ip, uint32_t len, int allow_fast_path)
{
	char *op = this->op;
	const int space_left = this->op_limit - op;
	/*Fast path, used for the majority (about 90%) of dynamic invocations.*/
	if (allow_fast_path && len <= 16 && space_left >= 16) {
		UNALIGNED_STORE64(op, UNALIGNED_LOAD64(ip));
		UNALIGNED_STORE64(op + 8, UNALIGNED_LOAD64(ip + 8));
	} else {
		if (space_left < len)
			return FALSE;
		memcpy(op, ip, len);
	}
	this->op = op + len;
	return TRUE;
}

static inline int
SAW__AppendFromSelf(struct SnappyArrayWriter *this,
		    uint32_t offset, uint32_t len)
{
	char *op = this->op;
	const int space_left = this->op_limit - op;
	/* -1u catches offset==0 */
	if (op - this->base <= offset - 1u)
		return FALSE;
	/* Fast path, used for the majority (70-80%) of dynamic invocations. */
	if (len <= 16 && offset >= 8 && space_left >= 16) {
		UNALIGNED_STORE64(op, UNALIGNED_LOAD64(op - offset));
		UNALIGNED_STORE64(op + 8, UNALIGNED_LOAD64(op - offset + 8));
	} else {
		if (space_left >= len + kMaxIncrementCopyOverflow) {
			IncrementalCopyFastPath(op - offset, op, len);
		} else {
			if (space_left < len)
				return FALSE;
			IncrementalCopy(op - offset, op, len);
		}
	}
	this->op = op + len;
	return TRUE;
}


/* Helper class for decompression */
struct SnappyDecompressor {
	struct ByteArraySource	*reader;	/* Underlying source of bytes to decompress */
	const char		*ip;		/* Points to next buffered byte */
	const char		*ip_limit;	/* Points just past buffered bytes */
	uint32_t		peeked;		/* Bytes peeked from reader (need to skip) */
	int			eof;		/* Hit end of input without an error? */
	char			scratch[5];	/* Temporary buffer for PeekFast() boundaries */
};

static inline void
SD__init(struct SnappyDecompressor *this, struct ByteArraySource *reader)
{
	this->reader = reader;
	this->ip = NULL;
	this->ip_limit = NULL;
	this->peeked = 0;
	this->eof = FALSE;
}

static inline void
SD__destroy(struct SnappyDecompressor *this)
{
	/* Advance past any bytes we peeked at from the reader */
	BAS__Skip(this->reader, this->peeked);
}

static inline uint32_t MIN_UINT32(uint32_t a, uint32_t b)
{
	if (a > b)
		return b;
	else
		return a;
}

/*
 * Ensure that all of the tag metadata for the next tag is available
 * in [ip_..ip_limit_-1].  Also ensures that [ip,ip+4] is readable even
 * if (ip_limit_ - ip_ < 5).
 *
 * Returns TRUE on success, FALSE on error or end of input.
 */
static int
SD__RefillTag(struct SnappyDecompressor *this)
{
	const char* ip = this->ip;
	if (ip == this->ip_limit) {
		/* Fetch a new fragment from the reader */
		/* All peeked bytes are used up */
		BAS__Skip(this->reader, this->peeked);
		size_t n;
		ip = BAS__Peek(this->reader, &n);
		this->peeked = n;
		if (n == 0) {
			this->eof = TRUE;
			return FALSE;
		}
		this->ip_limit = ip + n;
	}

	/* Read the tag character */
	DCHECK_LT(ip, this->ip_limit);
	const uint8_t c = *(const uint8_t*)ip;
	const uint32_t entry = char_table[c];
	const uint32_t needed = (entry >> 11) + 1; /* +1 byte for 'c' */
	DCHECK_LE(needed, sizeof(this->scratch));

	/* Read more bytes from reader if needed */
	uint32_t nbuf = this->ip_limit - ip;
	if (nbuf < needed) {
		/* Stitch together bytes from ip and reader to form the word
		   contents. We store the needed bytes in "scratch_". They
		   will be consumed immediately by the caller since we do not
		   read more than we need. */
		memmove(this->scratch, ip, nbuf);
		/* All peeked bytes are used up */
		BAS__Skip(this->reader, this->peeked);
		this->peeked = 0;
		while (nbuf < needed) {
			size_t length;
			const char* src = BAS__Peek(this->reader, &length);
			if (length == 0) return FALSE;
			uint32_t to_add = MIN_UINT32(needed - nbuf, length);
			memcpy(this->scratch + nbuf, src, to_add);
			nbuf += to_add;
			BAS__Skip(this->reader, to_add);
		}
		DCHECK_EQ(nbuf, needed);
		this->ip = this->scratch;
		this->ip_limit = this->scratch + needed;
	} else if (nbuf < 5) {
		/* Have enough bytes, but move into scratch_ so that we do not
		   read past end of input */
		memmove(this->scratch, ip, nbuf);
		BAS__Skip(this->reader, this->peeked); /* All peeked bytes are used up */
		this->peeked = 0;
		this->ip = this->scratch;
		this->ip_limit = this->scratch + nbuf;
	} else {
		/* Pass pointer to buffer returned by reader_. */
		this->ip = ip;
	}
	return TRUE;
}

/* Returns TRUE iff we have hit the end of the input without an error. */
static inline int
SD__eof(const struct SnappyDecompressor *this)
{
	return this->eof;
}

/*
 * Read the uncompressed length stored at the start of the compressed data.
 * On succcess, stores the length in *result and returns TRUE.
 * On failure, returns FALSE.
 */
static inline int
SD__ReadUncompressedLength(struct SnappyDecompressor *this, uint32_t *result)
{
	DCHECK(this->ip == NULL); /* Must not have read anything yet */
	/* Length is encoded in 1..5 bytes */
	*result = 0;
	uint32_t shift = 0;
	for(;;) {
		if (shift >= 32)
			return FALSE;
		size_t n;
		const char* ip = BAS__Peek(this->reader, &n);
		if (n == 0)
			return FALSE;
		const uint8_t c = *(const uint8_t*)ip;
		BAS__Skip(this->reader, 1);
		*result |= (uint32_t)(c & 0x7f) << shift;
		if (c < 128)
			break;
		shift += 7;
	}
	return TRUE;
}

/*
 * Process the next item found in the input.
 * Returns TRUE if successful, FALSE on error or end of input.
 */
static inline int
SD__Step(struct SnappyDecompressor *this, struct SnappyArrayWriter *writer)
{
	const char* ip = this->ip;
	if (this->ip_limit - ip < 5) {
		if (!SD__RefillTag(this))
			return FALSE;
		ip = this->ip;
	}

	const uint8_t c = *(const uint8_t*)(ip++);
	const uint32_t entry = char_table[c];
	const uint32_t trailer = le32_to_cpu(UNALIGNED_LOAD32(ip)) &
				 wordmask[entry >> 11];
	ip += entry >> 11;
	const uint32_t length = entry & 0xff;

	if ((c & 0x3) == LITERAL) {
		uint32_t literal_length = length + trailer;
		uint32_t avail = this->ip_limit - ip;
		while (avail < literal_length) {
			int allow_fast_path = (avail >= 16);
			if (!SAW__Append(writer, ip, avail, allow_fast_path))
				return FALSE;
			literal_length -= avail;
			BAS__Skip(this->reader, this->peeked);
			size_t n;
			ip = BAS__Peek(this->reader, &n);
			avail = n;
			this->peeked = avail;
			if (avail == 0)
				return FALSE; /* Premature end of input */
			this->ip_limit = ip + avail;
		}
		this->ip = ip + literal_length;
		int allow_fast_path = (avail >= 16);
		return SAW__Append(writer, ip, literal_length, allow_fast_path);
	} else {
		this->ip = ip;
		/* copy_offset/256 is encoded in bits 8..10.  By just fetching
		   those bits, we get copy_offset (since the bit-field starts at
		   bit 8). */
		const uint32_t copy_offset = entry & 0x700;
		return SAW__AppendFromSelf(writer, copy_offset + trailer, length);
	}
}


int
snappy_get_uncompressed_length(const char *start, size_t n, size_t *result)
{
	uint32_t v = 0;
	const char *limit = start + n;
	if (varint_parse32(start, limit, &v) != NULL) {
		*result = v;
		return TRUE;
	} else {
		return FALSE;
	}
}
#if defined(__KERNEL__) && !defined(STATIC)
EXPORT_SYMBOL(snappy_get_uncompressed_length);
#endif

int
snappy_decompress(const char *src, size_t src_len, char *dst, size_t dst_len)
{
	struct ByteArraySource reader;
	struct SnappyArrayWriter writer;
	struct SnappyDecompressor decompressor;
	BAS__init(&reader, src, src_len);
	SAW__init(&writer, dst);
	/* Read the uncompressed length from the front of the compressed input */
	SD__init(&decompressor, &reader);
	uint32_t uncompressed_len = 0;
	if (!SD__ReadUncompressedLength(&decompressor, &uncompressed_len))
		goto error;
	/* Protect against possible DoS attack */
	if ((size_t)uncompressed_len > dst_len)
		goto error;
	SAW__SetExpectedLength(&writer, uncompressed_len);
	/* Process the entire input */
	while (SD__Step(&decompressor, &writer)) { }
	int status = (SD__eof(&decompressor) && SAW__CheckLength(&writer));
	SD__destroy(&decompressor);
	if (status) return TRUE; else return FALSE;
error:
	SD__destroy(&decompressor);
	return FALSE;
}
#if defined(__KERNEL__) && !defined(STATIC)
EXPORT_SYMBOL(snappy_decompress);

MODULE_LICENSE("BSD");
MODULE_DESCRIPTION("Snappy Decompressor");
#endif

#ifdef TEST
#define MAX_INPUT_SIZE 10 * 1024 * 1024
#include <stdio.h>
int main(int argc, char *argv[])
{
	FILE *input_file, *output_file;
	if (argc < 3) {
		fprintf(stderr, "Usage: first argument is input file, "
				"second argument is output file.\n"
				"Use - for stdin/stdout.\n");
		return 1;
	}
	if (strcmp("-", argv[1]) == 0)
		input_file = stdin;
	else
		input_file = fopen(argv[1], "rb");
	if (strcmp("-", argv[2]) == 0)
		output_file = stdout;
	else
		output_file = fopen(argv[2], "wb");
	
	char *input_bufer = (char *)malloc(MAX_INPUT_SIZE);
	if (!input_bufer)
	{
		fprintf(stderr, "malloc failed to allocate %d.\n", MAX_INPUT_SIZE);
		fclose(input_file);
		fclose(output_file);
		return 2;
	}
	size_t input_len = fread(input_bufer, 1, MAX_INPUT_SIZE, input_file);
	if (!feof(input_file))
	{
		fprintf(stderr, "input was longer than %d, aborting.\n", MAX_INPUT_SIZE);
		free(input_bufer);
		fclose(input_file);
		fclose(output_file);
		return 3;
	}
	fclose(input_file);
	
	size_t uncompressed_len;
	if (snappy_get_uncompressed_length(input_bufer, input_len, &uncompressed_len) == FALSE)
	{
		fprintf(stderr, "snappy_get_uncompressed_length failed.\n");
		free(input_bufer);
		fclose(output_file);
		return 4;
	}
	
	char* output_buffer = (char *)malloc(uncompressed_len);
	if (!output_buffer)
	{
		fprintf(stderr, "malloc failed to allocate %d.\n", (int)uncompressed_len);
		free(input_bufer);
		fclose(output_file);
		return 2;
	}
	
	int status = snappy_decompress(input_bufer, input_len, output_buffer, uncompressed_len);
	free(input_bufer);
	if (status == FALSE)
	{
		fprintf(stderr, "snappy_decompress failed.\n");
		free(output_buffer);
		fclose(output_file);
		return 5;
	}
	
	fwrite(output_buffer, 1, uncompressed_len, output_file);
	fclose(output_file);
	
	free(output_buffer);
	
	return 0;
}
#endif
