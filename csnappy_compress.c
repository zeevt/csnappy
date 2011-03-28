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
#else
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define EXPORT_SYMBOL(x)
#endif


#ifdef HAVE_BUILTIN_CTZ

static inline int FindLSBSetNonZero(uint32_t n)
{
	return __builtin_ctz(n);
}

static inline int FindLSBSetNonZero64(uint64_t n)
{
	return __builtin_ctzll(n);
}

#else /* Portable versions. */

static inline int FindLSBSetNonZero(uint32_t n)
{
	int rc = 31;
	for (int i = 4, shift = 1 << 4; i >= 0; --i) {
		const uint32_t x = n << shift;
		if (x != 0) {
			n = x;
			rc -= shift;
		}
		shift >>= 1;
	}
	return rc;
}

/* FindLSBSetNonZero64() is defined in terms of FindLSBSetNonZero(). */
static inline int FindLSBSetNonZero64(uint64_t n)
{
	const uint32_t bottombits = (uint32_t)n;
	if (bottombits == 0) {
		/* Bottom bits are zero, so scan in top bits */
		return 32 + FindLSBSetNonZero((uint32_t)(n >> 32));
	} else {
		return FindLSBSetNonZero(bottombits);
	}
}

#endif /* End portable versions. */


/* Maximum lengths of varint encoding of uint32_t. */
static const int Varint__kMax32 = 5;

static inline char*
Varint__Encode32(char *sptr, uint32_t v)
{
	uint8_t* ptr = (uint8_t*)sptr;
	static const int B = 128;
	if (v < (1<<7)) {
		*(ptr++) = v;
	} else if (v < (1<<14)) {
		*(ptr++) = v | B;
		*(ptr++) = v>>7;
	} else if (v < (1<<21)) {
		*(ptr++) = v | B;
		*(ptr++) = (v>>7) | B;
		*(ptr++) = v>>14;
	} else if (v < (1<<28)) {
		*(ptr++) = v | B;
		*(ptr++) = (v>>7) | B;
		*(ptr++) = (v>>14) | B;
		*(ptr++) = v>>21;
	} else {
		*(ptr++) = v | B;
		*(ptr++) = (v>>7) | B;
		*(ptr++) = (v>>14) | B;
		*(ptr++) = (v>>21) | B;
		*(ptr++) = v>>28;
	}
	return (char*)ptr;
}


struct UncheckedByteArraySink {
	char *dest;
};

static inline void
UBAS__init(struct UncheckedByteArraySink *this, char *dest)
{
	this->dest = dest;
}

static inline void
UBAS__Append(struct UncheckedByteArraySink *this, const char *data, size_t n)
{
	/* Do no copying if the caller filled in the result of GetAppendBuffer() */
	if (data != this->dest)
		memcpy(this->dest, data, n);
	this->dest += n;
}

static inline char*
UBAS__GetAppendBuffer(struct UncheckedByteArraySink *this, size_t len, char *scratch)
{
	return this->dest;
}


/*
 * Any hash function will produce a valid compressed bitstream, but a good
 * hash function reduces the number of collisions and thus yields better
 * compression for compressible input, and more speed for incompressible
 * input. Of course, it doesn't hurt if the hash function is reasonably fast
 * either, as it gets called a lot.
 */
static inline uint32_t HashBytes(uint32_t bytes, int shift)
{
  uint32_t kMul = 0x1e35a7bd;
  return (bytes * kMul) >> shift;
}
static inline uint32_t Hash(const char *p, int shift)
{
  return HashBytes(UNALIGNED_LOAD32(p), shift);
}


/*
 * *** DO NOT CHANGE THE VALUE OF kBlockSize ***

 * New Compression code chops up the input into blocks of at most
 * the following size.  This ensures that back-references in the
 * output never cross kBlockSize block boundaries.  This can be
 * helpful in implementing blocked decompression.  However the
 * decompression code should not rely on this guarantee since older
 * compression code may not obey it.
 */
#define kBlockLog 15
#define kBlockSize (1 << kBlockLog)


/*
 * Return the largest n such that
 *
 *   s1[0,n-1] == s2[0,n-1]
 *   and n <= (s2_limit - s2).
 *
 * Does not read *s2_limit or beyond.
 * Does not read *(s1 + (s2_limit - s2)) or beyond.
 * Requires that s2_limit >= s2.
 *
 * Separate implementation for x86_64, for speed.  Uses the fact that
 * x86_64 is little endian.
 */
#if defined(__x86_64__)
static inline int
FindMatchLength(const char *s1, const char *s2, const char *s2_limit)
{
	DCHECK_GE(s2_limit, s2);
	int matched = 0;
	/*
	 * Find out how long the match is. We loop over the data 64 bits at a
	 * time until we find a 64-bit block that doesn't match; then we find
	 * the first non-matching bit and use that to calculate the total
	 * length of the match.
	 */
	while (likely(s2 <= s2_limit - 8)) {
		if (unlikely(UNALIGNED_LOAD64(s2) == UNALIGNED_LOAD64(s1 + matched))) {
			s2 += 8;
			matched += 8;
		} else {
			/*
			 * On current (mid-2008) Opteron models there is a 3% more
			 * efficient code sequence to find the first non-matching byte.
			 * However, what follows is ~10% better on Intel Core 2 and newer,
			 * and we expect AMD's bsf instruction to improve.
			 */
			uint64_t x = UNALIGNED_LOAD64(s2) ^ UNALIGNED_LOAD64(s1 + matched);
			int matching_bits = FindLSBSetNonZero64(x);
			matched += matching_bits >> 3;
			return matched;
		}
	}
	while (likely(s2 < s2_limit)) {
		if (likely(s1[matched] == *s2)) {
			++s2;
			++matched;
		} else {
			return matched;
		}
	}
	return matched;
}
#else
static inline int
FindMatchLength(const char *s1, const char *s2, const char *s2_limit)
{
	/* Implementation based on the x86-64 version, above. */
	DCHECK_GE(s2_limit, s2);
	int matched = 0;

	while (s2 <= s2_limit - 4 &&
		UNALIGNED_LOAD32(s2) == UNALIGNED_LOAD32(s1 + matched)) {
		s2 += 4;
		matched += 4;
	}
#ifdef __LITTLE_ENDIAN
	if (s2 <= s2_limit - 4) {
		uint32_t x = UNALIGNED_LOAD32(s2) ^ UNALIGNED_LOAD32(s1 + matched);
		int matching_bits = FindLSBSetNonZero(x);
		matched += matching_bits >> 3;
	} else {
#else
		while ((s2 < s2_limit) && (s1[matched] == *s2)) {
			++s2;
			++matched;
		}
#endif
#ifdef __LITTLE_ENDIAN
	}
#endif
	return matched;
}
#endif


static inline char*
EmitLiteral(char *op, const char *literal, int len, int allow_fast_path)
{
	int n = len - 1; /* Zero-length literals are disallowed */
	if (n < 60) {
		/* Fits in tag byte */
		*op++ = LITERAL | (n << 2);
		/*
		The vast majority of copies are below 16 bytes, for which a
		call to memcpy is overkill. This fast path can sometimes
		copy up to 15 bytes too much, but that is okay in the
		main loop, since we have a bit to go on for both sides:
		- The input will always have kInputMarginBytes = 15 extra
		available bytes, as long as we're in the main loop, and
		if not, allow_fast_path = false.
		- The output will always have 32 spare bytes (see
		snappy_max_compressed_length).
		*/
		if (allow_fast_path && len <= 16) {
			UNALIGNED_STORE64(op, UNALIGNED_LOAD64(literal));
			UNALIGNED_STORE64(op + 8, UNALIGNED_LOAD64(literal + 8));
			return op + len;
		}
	} else {
		/* Encode in upcoming bytes */
		char* base = op;
		int count = 0;
		op++;
		while (n > 0) {
			*op++ = n & 0xff;
			n >>= 8;
			count++;
		}
		assert(count >= 1);
		assert(count <= 4);
		*base = LITERAL | ((59+count) << 2);
	}
	memcpy(op, literal, len);
	return op + len;
}

static inline char*
EmitCopyLessThan64(char* op, int offset, int len)
{
	DCHECK_LE(len, 64);
	DCHECK_GE(len, 4);
	DCHECK_LT(offset, 65536);

	if ((len < 12) && (offset < 2048)) {
		int len_minus_4 = len - 4;
		assert(len_minus_4 < 8); /* Must fit in 3 bits */
		*op++ = COPY_1_BYTE_OFFSET | ((len_minus_4) << 2) | ((offset >> 8) << 5);
		*op++ = offset & 0xff;
	} else {
		*op++ = COPY_2_BYTE_OFFSET | ((len-1) << 2);
		UNALIGNED_STORE16(op, cpu_to_le16(offset));
		op += 2;
	}
	return op;
}

static inline char*
EmitCopy(char* op, int offset, int len)
{
	/* Emit 64 byte copies but make sure to keep at least four bytes reserved */
	while (len >= 68) {
		op = EmitCopyLessThan64(op, offset, 64);
		len -= 64;
	}

	/* Emit an extra 60 byte copy if have too much data to fit in one copy */
	if (len > 64) {
		op = EmitCopyLessThan64(op, offset, 60);
		len -= 60;
	}

	/* Emit remainder */
	op = EmitCopyLessThan64(op, offset, len);
	return op;
}


/*
 * For 0 <= offset <= 4, GetUint32AtOffset(UNALIGNED_LOAD64(p), offset) will
 * equal UNALIGNED_LOAD32(p + offset).  Motivation: On x86-64 hardware we have
 * empirically found that overlapping loads such as
 *  UNALIGNED_LOAD32(p) ... UNALIGNED_LOAD32(p+1) ... UNALIGNED_LOAD32(p+2)
 * are slower than UNALIGNED_LOAD64(p) followed by shifts and casts to uint32_t.
 */
static inline uint32_t
GetUint32AtOffset(uint64_t v, int offset)
{
	DCHECK(0 <= offset && offset <= 4);
#ifdef __LITTLE_ENDIAN
	return v >> (8 * offset);
#else
	return v >> (32 - 8 * offset);
#endif
}

/*
 * Flat array compression that does not emit the "uncompressed length"
 * prefix. Compresses "input" string to the "*op" buffer.
 *
 * REQUIRES: "input" is at most "kBlockSize" bytes long.
 * REQUIRES: "op" points to an array of memory that is at least
 * "snappy_max_compressed_length(input.size())" in size.
 * REQUORES: working_memory points to a region of (2^workmem_bytes_power_of_two)
 * bytes, all set to zero before snappy_compress_fragment is called.
 * REQUORES: 9 <= workmem_bytes_power_of_two <= 15.
 *
 * Returns an "end" pointer into "op" buffer.
 * "end - op" is the compressed size of "input".
 */
static inline char*
snappy_compress_fragment(
	const char const *input,
	const size_t input_size,
	char *op,
	void *working_memory,
	const int workmem_bytes_power_of_two)
{
	DCHECK(9 <= workmem_bytes_power_of_two && workmem_bytes_power_of_two <= 15);
	/* Table of 2^X bytes. Need only X-1 bits of 32bit key to address uint16_t. */
	const int shift = 33 - workmem_bytes_power_of_two;
	uint16_t *table = (uint16_t*)working_memory;
	/* "ip" is the input pointer, and "op" is the output pointer. */
	const char* ip = input;
	assert(input_size <= kBlockSize);
	const char* ip_end = input + input_size;
	const char* base_ip = ip;
	/* Bytes in [next_emit, ip) will be emitted as literal bytes. Or
	   [next_emit, ip_end) after the main loop. */
	const char* next_emit = ip;

	const int kInputMarginBytes = 15;
	if (unlikely(input_size < kInputMarginBytes))
		goto emit_remainder;

	const char* ip_limit = input + input_size - kInputMarginBytes;
	uint32_t next_hash = Hash(++ip, shift);

	main_loop:
	DCHECK_LT(next_emit, ip);
	/*
	* The body of this loop calls EmitLiteral once and then EmitCopy one or
	* more times. (The exception is that when we're close to exhausting
	* the input we goto emit_remainder.)
	*
	* In the first iteration of this loop we're just starting, so
	* there's nothing to copy, so calling EmitLiteral once is
	* necessary. And we only start a new iteration when the
	* current iteration has determined that a call to EmitLiteral will
	* precede the next call to EmitCopy (if any).
	*
	* Step 1: Scan forward in the input looking for a 4-byte-long match.
	* If we get close to exhausting the input then goto emit_remainder.
	*
	* Heuristic match skipping: If 32 bytes are scanned with no matches
	* found, start looking only at every other byte. If 32 more bytes are
	* scanned, look at every third byte, etc.. When a match is found,
	* immediately go back to looking at every byte. This is a small loss
	* (~5% performance, ~0.1% density) for compressible data due to more
	* bookkeeping, but for non-compressible data (such as JPEG) it's a huge
	* win since the compressor quickly "realizes" the data is incompressible
	* and doesn't bother looking for matches everywhere.
	*
	* The "skip" variable keeps track of how many bytes there are since the
	* last match; dividing it by 32 (ie. right-shifting by five) gives the
	* number of bytes to move ahead for each iteration.
	*/
	uint32_t skip = 32;

	const char* next_ip = ip;
	const char* candidate;
	do {
		ip = next_ip;
		uint32_t hash = next_hash;
		DCHECK_EQ(hash, Hash(ip, shift));
		uint32_t bytes_between_hash_lookups = skip++ >> 5;
		next_ip = ip + bytes_between_hash_lookups;
		if (unlikely(next_ip > ip_limit))
			goto emit_remainder;
		next_hash = Hash(next_ip, shift);
		candidate = base_ip + table[hash];
		DCHECK_GE(candidate, base_ip);
		DCHECK_LT(candidate, ip);

		table[hash] = ip - base_ip;
	} while (likely(UNALIGNED_LOAD32(ip) !=
			UNALIGNED_LOAD32(candidate)));

	/*
	* Step 2: A 4-byte match has been found. We'll later see if more
	* than 4 bytes match. But, prior to the match, input
	* bytes [next_emit, ip) are unmatched. Emit them as "literal bytes."
	*/
	DCHECK_LE(next_emit + 16, ip_end);
	op = EmitLiteral(op, next_emit, ip - next_emit, TRUE);

	/*
	* Step 3: Call EmitCopy, and then see if another EmitCopy could
	* be our next move. Repeat until we find no match for the
	* input immediately after what was consumed by the last EmitCopy call.
	*
	* If we exit this loop normally then we need to call EmitLiteral next,
	* though we don't yet know how big the literal will be. We handle that
	* by proceeding to the next iteration of the main loop. We also can exit
	* this loop via goto if we get close to exhausting the input.
	*/
	uint64_t input_bytes = 0;
	uint32_t candidate_bytes = 0;

	do {
		/* We have a 4-byte match at ip, and no need to emit any
		 "literal bytes" prior to ip. */
		const char* base = ip;
		int matched = 4 + FindMatchLength(candidate + 4, ip + 4, ip_end);
		ip += matched;
		int offset = base - candidate;
		DCHECK_EQ(0, memcmp(base, candidate, matched));
		op = EmitCopy(op, offset, matched);
		/* We could immediately start working at ip now, but to improve
		 compression we first update table[Hash(ip - 1, ...)]. */
		const char* insert_tail = ip - 1;
		next_emit = ip;
		if (unlikely(ip >= ip_limit))
			goto emit_remainder;
		input_bytes = UNALIGNED_LOAD64(insert_tail);
		uint32_t prev_hash = HashBytes(GetUint32AtOffset(input_bytes, 0), shift);
		table[prev_hash] = ip - base_ip - 1;
		uint32_t cur_hash = HashBytes(GetUint32AtOffset(input_bytes, 1), shift);
		candidate = base_ip + table[cur_hash];
		candidate_bytes = UNALIGNED_LOAD32(candidate);
		table[cur_hash] = ip - base_ip;
	} while (GetUint32AtOffset(input_bytes, 1) == candidate_bytes);

	next_hash = HashBytes(GetUint32AtOffset(input_bytes, 2), shift);
	++ip;
	goto main_loop;

	emit_remainder:
	/* Emit the remaining bytes as a literal */
	if (next_emit < ip_end)
		op = EmitLiteral(op, next_emit, ip_end - next_emit, FALSE);

	return op;
}
EXPORT_SYMBOL(snappy_compress_fragment);

/*
 * Returns the maximal size of the compressed representation of
 * input data that is "source_len" bytes in length;
 */
size_t
snappy_max_compressed_length(size_t source_len)
{
	return 32 + source_len + source_len/6;
}
EXPORT_SYMBOL(snappy_max_compressed_length);


static inline int MIN_int(int a, int b)
{
	if (a > b) return b;
	else return a;
}
static inline size_t MIN_sizet(size_t a, size_t b)
{
	if (a > b) return b;
	else return a;
}

/*
 * REQUIRES: "compressed" must point to an area of memory that is at
 * least "snappy_max_compressed_length(input_length)" bytes in length.
 * REQUORES: working_memory points to a region of (2^workmem_bytes_power_of_two)
 * bytes.
 * REQUORES: 9 <= workmem_bytes_power_of_two <= 15.
 *
 * Takes the data stored in "input[0..input_length]" and stores
 * it in the array pointed to by "compressed".
 *
 * "*compressed_length" is set to the length of the compressed output.
 */
void
snappy_compress(
	const char *input,
	size_t input_length,
	char *compressed,
	size_t *compressed_length,
	void *working_memory,
	const int workmem_bytes_power_of_two)
{
	struct ByteArraySource reader;
	struct UncheckedByteArraySink writer;
	BAS__init(&reader, input, input_length);
	UBAS__init(&writer, compressed);

	size_t written = 0;
	int N = BAS__Available(&reader);
	char ulength[Varint__kMax32];
	char *p = Varint__Encode32(ulength, N);
	UBAS__Append(&writer, ulength, p - ulength);
	written += (p - ulength);

	char *scratch = NULL;
	char *scratch_output = NULL;

	while (N > 0) {
		/* Get next block to compress (without copying if possible) */
		size_t fragment_size;
		const char *fragment = BAS__Peek(&reader, &fragment_size);
		DCHECK_NE(fragment_size, 0);
		const int num_to_read = MIN_int(N, kBlockSize);
		size_t bytes_read = fragment_size;

		int pending_advance = 0;
		if (bytes_read >= num_to_read) {
			/* Buffer returned by reader is large enough */
			pending_advance = num_to_read;
			fragment_size = num_to_read;
		} else {
			/* Read into scratch buffer.
			 * If this is the last iteration, we want to allocate N bytes
			 * of space, otherwise the max possible kBlockSize space.
			 * num_to_read contains exactly the correct value */
			if (scratch == NULL)
				scratch = malloc(num_to_read);
			memcpy(scratch, fragment, bytes_read);
			BAS__Skip(&reader, bytes_read);

			while (bytes_read < num_to_read) {
				fragment = BAS__Peek(&reader, &fragment_size);
				size_t n = MIN_sizet(fragment_size, num_to_read - bytes_read);
				memcpy(scratch + bytes_read, fragment, n);
				bytes_read += n;
				BAS__Skip(&reader, n);
			}
			DCHECK_EQ(bytes_read, num_to_read);
			fragment = scratch;
			fragment_size = num_to_read;
		}
		DCHECK_EQ(fragment_size, num_to_read);

		/* Compress input_fragment and append to dest */
		const int max_output = snappy_max_compressed_length(num_to_read);

		/* Need a scratch buffer for the output, in case the byte sink doesn't
		 * have room for us directly.
		 * Since we encode kBlockSize regions followed by a region
		 * which is <= kBlockSize in length, a previously allocated
		 * scratch_output[] region is big enough for this iteration. */
		if (scratch_output == NULL)
			scratch_output = malloc(max_output);
		char *dest = UBAS__GetAppendBuffer(&writer, max_output, scratch_output);
		memset(working_memory, 0, 1 << workmem_bytes_power_of_two);
		char *end = snappy_compress_fragment(
				fragment, fragment_size, dest,
				working_memory, workmem_bytes_power_of_two);
		UBAS__Append(&writer, dest, end - dest);
		written += (end - dest);

		N -= num_to_read;
		BAS__Skip(&reader, pending_advance);
	}

	free(scratch);
	free(scratch_output);
	*compressed_length = written;
}
EXPORT_SYMBOL(snappy_compress);

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

	size_t max_compressed_len = snappy_max_compressed_length(input_len);
	char *output_buffer = (char*)malloc(max_compressed_len);
	if (!output_buffer)
	{
		fprintf(stderr, "malloc failed to allocate %d bytes.\n", (int)max_compressed_len);
		free(input_bufer);
		fclose(output_file);
		return 2;
	}
#define SNAPPY_WORKMEM_BYTES_POWER_OF_TWO 15
#define SNAPPY_WORKMEM_BYTES (1 << SNAPPY_WORKMEM_BYTES_POWER_OF_TWO)
	void *working_memory = malloc(SNAPPY_WORKMEM_BYTES);
	if (!working_memory)
	{
		fprintf(stderr, "malloc failed to allocate %d bytes.\n", SNAPPY_WORKMEM_BYTES);
		free(input_bufer);
		fclose(output_file);
		return 2;
	}

	size_t compressed_len;
	snappy_compress(input_bufer, input_len, output_buffer, &compressed_len,
			working_memory, SNAPPY_WORKMEM_BYTES_POWER_OF_TWO);
	free(input_bufer);

	fwrite(output_buffer, 1, compressed_len, output_file);
	fclose(output_file);

	free(output_buffer);

	return 0;
}
#endif
