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

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include "linux/csnappy.h"
#else
#include "csnappy.h"
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
			return SNAPPY_E_OUTPUT_OVERRUN;
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
		return SNAPPY_E_DATA_MALFORMED;
	/* Fast path, used for the majority (70-80%) of dynamic invocations. */
	if (len <= 16 && offset >= 8 && space_left >= 16) {
		UNALIGNED_STORE64(op, UNALIGNED_LOAD64(op - offset));
		UNALIGNED_STORE64(op + 8, UNALIGNED_LOAD64(op - offset + 8));
	} else if (space_left >= len + kMaxIncrementCopyOverflow) {
		IncrementalCopyFastPath(op - offset, op, len);
	} else {
		if (space_left < len)
			return SNAPPY_E_OUTPUT_OVERRUN;
		IncrementalCopy(op - offset, op, len);
	}
	this->op = op + len;
	return TRUE;
}


/* Helper class for decompression */
struct SnappyDecompressor {
	const char	*src;
	uint32_t	src_bytes_left;
	const char	*ip;		/* Points to next buffered byte */
	const char	*ip_limit;	/* Points just past buffered bytes */
	uint32_t	peeked;		/* Bytes peeked from reader (need to skip) */
	int		eof;		/* Hit end of input without an error? */
	char		scratch[5];	/* Temporary buffer for PeekFast() boundaries */
};

static inline void
SD__init(struct SnappyDecompressor *this, const char *source, uint32_t src_len)
{
	this->src = source;
	this->src_bytes_left = src_len;
	this->ip = NULL;
	this->ip_limit = NULL;
	this->peeked = 0;
	this->eof = FALSE;
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
 * Returns TRUE on success.
 */
static int
SD__RefillTag(struct SnappyDecompressor *this)
{
	const char* ip = this->ip;
	uint32_t needed, nbuf, to_add;
	if (ip == this->ip_limit) {
		/* Fetch a new fragment from the reader */
		/* All peeked bytes are used up */
		this->src += this->peeked;
		this->src_bytes_left -= this->peeked;
		ip = this->src;
		this->peeked = this->src_bytes_left;
		if (this->src_bytes_left == 0) {
			this->eof = TRUE;
			return SNAPPY_E_OK;
		}
		this->ip_limit = ip + this->src_bytes_left;
	}

	/* Read the tag character */
	DCHECK_LT(ip, this->ip_limit);
	/* +1 byte for current byte at ip */
	needed = (char_table[*(const uint8_t*)ip] >> 11) + 1;
	DCHECK_LE(needed, sizeof(this->scratch));

	/* Read more bytes from reader if needed */
	nbuf = this->ip_limit - ip;
	if (nbuf < needed) {
		/* Stitch together bytes from ip and reader to form the word
		   contents. We store the needed bytes in "scratch_". They
		   will be consumed immediately by the caller since we do not
		   read more than we need. */
		memmove(this->scratch, ip, nbuf);
		/* All peeked bytes are used up */
		this->src += this->peeked;
		this->src_bytes_left -= this->peeked;
		this->peeked = 0;
		while (nbuf < needed) {
			if (this->src_bytes_left == 0)
				return SNAPPY_E_OUTPUT_OVERRUN;
			to_add = MIN_UINT32(needed - nbuf, this->src_bytes_left);
			memcpy(this->scratch + nbuf, this->src, to_add);
			nbuf += to_add;
			this->src += to_add;
			this->src_bytes_left -= to_add;
		}
		DCHECK_EQ(nbuf, needed);
		this->ip = this->scratch;
		this->ip_limit = this->scratch + needed;
	} else if (nbuf < 5) {
		/* Have enough bytes, but move into scratch_ so that we do not
		   read past end of input */
		memmove(this->scratch, ip, nbuf);
		/* All peeked bytes are used up */
		this->src += this->peeked;
		this->src_bytes_left -= this->peeked;
		this->peeked = 0;
		this->ip = this->scratch;
		this->ip_limit = this->scratch + nbuf;
	} else {
		/* Pass pointer to buffer returned by reader_. */
		this->ip = ip;
	}
	return TRUE;
}

/*
 * Read the uncompressed length stored at the start of the compressed data.
 * On succcess, stores the length in *result and returns SNAPPY_E_OK.
 * On failure, returns SNAPPY_E_HEADER_BAD.
 */
static noinline int
SD__ReadUncompressedLength(struct SnappyDecompressor *this, uint32_t *result)
{
	uint32_t shift = 0;
	uint8_t c;
	DCHECK(this->ip == NULL); /* Must not have read anything yet */
	/* Length is encoded in 1..5 bytes */
	*result = 0;
	for(;;) {
		if (shift >= 32)
			return SNAPPY_E_HEADER_BAD;
		if (this->src_bytes_left == 0)
			return SNAPPY_E_HEADER_BAD;
		c = *(const uint8_t*)this->src;
		this->src += 1;
		this->src_bytes_left -= 1;
		*result |= (uint32_t)(c & 0x7f) << shift;
		if (c < 128)
			break;
		shift += 7;
	}
	return SNAPPY_E_OK;
}

/*
 * Process the next item found in the input.
 * Returns TRUE if more data is pending.
 */
static inline int
SD__Step(struct SnappyDecompressor *this, struct SnappyArrayWriter *writer)
{
	uint8_t c;
	uint32_t entry, trailer, length, literal_length, avail;
	int ret, allow_fast_path;
	const char* ip = this->ip;
	if (this->ip_limit - ip < 5) {
		if ((ret = SD__RefillTag(this)) != TRUE)
			return ret;
		ip = this->ip;
	}

	c = *(const uint8_t*)(ip++);
	entry = char_table[c];
	trailer = le32_to_cpu(UNALIGNED_LOAD32(ip)) &
				wordmask[entry >> 11];
	ip += entry >> 11;
	length = entry & 0xff;

	if ((c & 0x3) == LITERAL) {
		literal_length = length + trailer;
		avail = this->ip_limit - ip;
		while (avail < literal_length) {
			allow_fast_path = (avail >= 16);
			ret = SAW__Append(writer, ip, avail, allow_fast_path);
			if (ret != TRUE)
				return ret;
			literal_length -= avail;
			this->src += this->peeked;
			this->src_bytes_left -= this->peeked;
			ip = this->src;
			avail = this->src_bytes_left;
			this->peeked = avail;
			if (avail == 0)
				return SNAPPY_E_INPUT_NOT_CONSUMED;
			this->ip_limit = ip + avail;
		}
		this->ip = ip + literal_length;
		allow_fast_path = (avail >= 16);
		return SAW__Append(writer, ip, literal_length, allow_fast_path);
	} else {
		this->ip = ip;
		/* copy_offset/256 is encoded in bits 8..10.  By just fetching
		   those bits, we get copy_offset (since the bit-field starts at
		   bit 8). */
		return SAW__AppendFromSelf(writer, (entry & 0x700) + trailer, length);
	}
}


int
snappy_get_uncompressed_length(const char *start, uint32_t n, uint32_t *result)
{
	struct SnappyDecompressor decomp;
	SD__init(&decomp, start, n);
	return SD__ReadUncompressedLength(&decomp, result);
}
#if defined(__KERNEL__) && !defined(STATIC)
EXPORT_SYMBOL(snappy_get_uncompressed_length);
#endif

int
snappy_decompress(const char *src, uint32_t src_len, char *dst, uint32_t dst_len)
{
	struct SnappyArrayWriter writer;
	struct SnappyDecompressor decomp;
	int ret;
	uint32_t olen = 0;
	writer.base = writer.op = dst;
	SD__init(&decomp, src, src_len);
	/* Read the uncompressed length from the front of the compressed input */
	ret = SD__ReadUncompressedLength(&decomp, &olen);
	if (unlikely(ret != SNAPPY_E_OK))
		return ret;
	/* Protect against possible DoS attack */
	if (unlikely(olen > dst_len))
		return SNAPPY_E_OUTPUT_INSUF;
	writer.op_limit = writer.op + olen;
	/* Process the entire input */
	while ((ret = SD__Step(&decomp, &writer)) == TRUE) { }
	if (ret != SNAPPY_E_OK)
		return ret;
	if (decomp.eof != TRUE)
		return SNAPPY_E_INPUT_NOT_CONSUMED;
	if (writer.op != writer.op_limit)
		return SNAPPY_E_UNEXPECTED_OUTPUT_LEN;
	return SNAPPY_E_OK;
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
	FILE *ifile, *ofile;
	char *ibuf, *obuf;
	uint32_t ilen, olen;
	int status;
	if (argc < 3) {
		fprintf(stderr, "Usage: first argument is input file, "
				"second argument is output file.\n"
				"Use - for stdin/stdout.\n");
		return 1;
	}
	if (strcmp("-", argv[1]) == 0)
		ifile = stdin;
	else
		ifile = fopen(argv[1], "rb");
	if (strcmp("-", argv[2]) == 0)
		ofile = stdout;
	else
		ofile = fopen(argv[2], "wb");
	
	if (!(ibuf = (char *)malloc(MAX_INPUT_SIZE)))
	{
		fprintf(stderr, "malloc failed to allocate %d.\n", MAX_INPUT_SIZE);
		fclose(ifile);
		fclose(ofile);
		return 2;
	}
	ilen = fread(ibuf, 1, MAX_INPUT_SIZE, ifile);
	if (!feof(ifile))
	{
		fprintf(stderr, "input was longer than %d, aborting.\n", MAX_INPUT_SIZE);
		free(ibuf);
		fclose(ifile);
		fclose(ofile);
		return 3;
	}
	fclose(ifile);
	
	if ((status = snappy_get_uncompressed_length(ibuf, ilen, &olen)) != SNAPPY_E_OK)
	{
		fprintf(stderr, "snappy_get_uncompressed_length returned %d.\n", status);
		free(ibuf);
		fclose(ofile);
		return 4;
	}
	
	if (!(obuf = (char *)malloc(olen)))
	{
		fprintf(stderr, "malloc failed to allocate %d.\n", (int)olen);
		free(ibuf);
		fclose(ofile);
		return 2;
	}
	
	status = snappy_decompress(ibuf, ilen, obuf, olen);
	free(ibuf);
	if (status != SNAPPY_E_OK)
	{
		fprintf(stderr, "snappy_decompress returned %d.\n", status);
		free(obuf);
		fclose(ofile);
		return 5;
	}
	
	fwrite(obuf, 1, olen, ofile);
	fclose(ofile);
	
	free(obuf);
	
	return 0;
}
#endif
