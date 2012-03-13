/*
Copyright 2011, Zeev Tarantov <zeev.tarantov@gmail.com>.
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
  * Neither the name of Zeev Tarantov nor the names of its
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
*/

/* 
 * A reimplementation of Google's Snappy compression algorithm for clarity
 *  instead of speed.
 * 
 * Snappy format:
 * Two types of commands:
 * 1. Output a literal x bytes long.
 * 2. Output a copy of bytes already in the output, of length x that is y bytes
 *     back from current position in the output.
 * Each begins with an opcode byte. The opcode byte uses the low 2 bits to
 *  encode the command type (0 to 3 means, in order: literal,
 *  copy with 1 byte offset, copy with 2 bytes offset and copy with 4 bytes
 *  offset). The high 6 bits of the opcode byte convey different information,
 *  depending on the command type.
 * Google's code employs an optimization that uses a precomputed table to
 *  expand the opcode byte into an opcode word, to speed up decoding.
 * I will describe the straightforward decoding of the opcode byte.
 * 
 * 1. Command type 0 - literal:
 * For literals in range [1..60], (len-1) is encoded in upper 6 bits of opcode.
 * For bigger literals, the length of the literal (minus one) is encoded in
 *  separate bytes following the opcode byte, in little endian byte order, and
 *  only after the length does the actual literal data follow. The number of
 *  bytes that encode the literal's length equals the upper 6 bits of the opcode
 *  byte - 59. Possible values are [60..63] for [1..4] bytes of length.
 * 2. Command type 1 - copy with 1 byte offset:
 * For length in range [4..11] and offset in range [0..2047].
 * Bits 2..4 of the opcode encode (length-4) in three bits, and bits 5-7 encode
 * (offset / 256) in three bits. One extra byte after the opcode byte encodes
 * (offset % 256).
 * 3. Command type 2 - copy with 2 byte offset:
 * The upper 6 bits of the opcode byte encode (len-1) and offset is encoded in
 * two bytes following the opcode byte.
 * 3. Command type 3 - copy with 4 byte offset:
 * The upper 6 bits of the opcode byte encode (len-1) and offset is encoded in
 * four bytes following the opcode byte.
 *
 * Notice that the encoder now encodes independent chunks 32KB long, so a back
 * offset that does not fit into 2 bytes is not produced in practice.
 * 
 * The compressed stream itself is preceded by a header which encodes the
 *  uncompressed length of the data within. The length, up to 32 bits long, is
 *  divided into groups of 7 bits, with the 7 least significant bits of the
 *  length as the first group and so on, and put into the lowest 7 bits of as
 *  many bytes as are needed (up to 5 bytes). The byte containing the least
 *  significant 7 bits of the length goes first, then if necessary the next
 *  byte and so on.
 * The last byte written, the one containing the most significant bits of the
 *  length, has an MSB of 0. For lengths <= 127, it is the only byte written.
 * Bytes preceding the last byte of the header have MSB of 1 and signify that
 *  more bytes of the header need to be read.
 */
#include <stdint.h>
#include <string.h>
#include "csnappy.h"

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

/*
 * RETURNS number of bytes read from src.
 * If header is malformed, returns < 0.
 * To decompress, pass to csnappy_decompress_noheader the src
 * incremented by this number and src_len decremented by this number.
 * IFF successfully parses header, puts output length into *uncompressed_len.
 */
int __attribute__((noinline)) csnappy_get_uncompressed_length(
	const char	*src,
	uint32_t	src_len,
	uint32_t	*uncompressed_len)
{
	int shift = 0, bytes_read = 0;
	uint8_t b;
	*uncompressed_len = 0;
	for (;;) {
		if (unlikely(bytes_read >= src_len))
			return CSNAPPY_E_HEADER_BAD;
		b = *(const uint8_t*)src++;
		bytes_read++;
		*uncompressed_len |= (b & 127) << shift;
		if (b < 128)
			break;
		shift += 7;
		if (unlikely(shift > 32))
			return CSNAPPY_E_HEADER_BAD;
	}
	return bytes_read;
}

/*
 * Uncompresses stream with no header.
 * *dst_len is read to get destination space and after successful decompression
 *  is set to number of decompressed bytes.
 * IFF successful, returns CSNAPPY_E_OK.
 * Never writes past *dst_len bytes into dst.
 */
int csnappy_decompress_noheader(
	const char	*src_,
	uint32_t	src_remaining,
	char		*dst,
	uint32_t	*dst_len)
{
	const uint8_t * src = (const uint8_t *)src_;
	const uint8_t * const src_end = src + src_remaining;
	char * const dst_base = dst;
	char * const dst_end = dst + *dst_len;
	while (src < src_end) {
		uint32_t opcode = *src++;
		uint32_t length = (opcode >> 2) + 1;
		const uint8_t *copy_src;
		if (likely((opcode & 3) == 0)) {
			if (unlikely(length > 60)) {
				uint32_t extra_bytes = length - 60;
				if (unlikely(src + extra_bytes > src_end))
					return CSNAPPY_E_DATA_MALFORMED;
				length = 0;
				for (int shift = 0, max_shift = extra_bytes*8;
					shift < max_shift;
					shift += 8)
					length |= *src++ << shift;
				++length;
			}
			if (unlikely(src + length > src_end))
				return CSNAPPY_E_DATA_MALFORMED;
			copy_src = src;
			src += length;
		} else {
			uint32_t offset;
			if (likely((opcode & 3) == 1)) {
				if (unlikely(src + 1 > src_end))
					return CSNAPPY_E_DATA_MALFORMED;
				length = ((length - 1) & 7) + 4;
				offset = ((opcode >> 5) << 8) + *src++;
			} else if (likely((opcode & 3) == 2)) {
				if (unlikely(src + 2 > src_end))
					return CSNAPPY_E_DATA_MALFORMED;
				offset = src[0] | (src[1] << 8);
				src += 2;
			} else {
				if (unlikely(src + 4 > src_end))
					return CSNAPPY_E_DATA_MALFORMED;
				offset = src[0] | (src[1] << 8) |
					 (src[2] << 16) | (src[3] << 24);
				src += 4;
			}
			if (unlikely(!offset || (offset > dst - dst_base)))
				return CSNAPPY_E_DATA_MALFORMED;
			copy_src = (const uint8_t *)dst - offset;
		}
		if (unlikely(dst + length > dst_end))
			return CSNAPPY_E_OUTPUT_OVERRUN;
		do *dst++ = *copy_src++; while (--length);
	}
	*dst_len = dst - dst_base;
	return CSNAPPY_E_OK;
}

int csnappy_decompress(
	const char	*src,
	uint32_t	src_len,
	char		*dst,
	uint32_t	dst_len)
{
	uint32_t compressed_len;
	int bytes_read = csnappy_get_uncompressed_length(
		src, src_len, &compressed_len);
	if (unlikely(bytes_read < 0))
		return bytes_read;
	if (unlikely(dst_len < compressed_len))
		return CSNAPPY_E_OUTPUT_INSUF;
	return csnappy_decompress_noheader(
		src + bytes_read, src_len - bytes_read, dst, &compressed_len);
}
