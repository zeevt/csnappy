#ifndef __CSNAPPY_H__
#define __CSNAPPY_H__
/*
File modified for the Linux Kernel by
Zeev Tarantov <zeev.tarantov@gmail.com>
*/

#define SNAPPY_WORKMEM_BYTES_POWER_OF_TWO 15
#define SNAPPY_WORKMEM_BYTES (1 << SNAPPY_WORKMEM_BYTES_POWER_OF_TWO)

/*
 * Returns the maximal size of the compressed representation of
 * input data that is "source_len" bytes in length;
 */
size_t
snappy_max_compressed_length(size_t source_len) __attribute__((const));

/*
 * Flat array compression that does not emit the "uncompressed length"
 * prefix. Compresses "input" string to the "*op" buffer.
 *
 * REQUIRES: "input" is at most "kBlockSize" bytes long.
 * REQUIRES: "op" points to an array of memory that is at least
 * "snappy_max_compressed_length(input.size())" in size.
 * REQUIRES: working_memory has (1 << workmem_bytes_power_of_two) NUL bytes.
 * REQUIRES: 9 <= workmem_bytes_power_of_two <= 15.
 *
 * Returns an "end" pointer into "op" buffer.
 * "end - op" is the compressed size of "input".
 */
char*
snappy_compress_fragment(
	const char* const input,
	const size_t input_size,
	char *op,
	void *working_memory,
	const int workmem_bytes_power_of_two);

/*
 * REQUIRES: "compressed" must point to an area of memory that is at
 * least "snappy_max_compressed_length(input_length)" bytes in length.
 * REQUIRES: working_memory has (1 << workmem_bytes_power_of_two) bytes.
 * REQUIRES: 9 <= workmem_bytes_power_of_two <= 15.
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
	const int workmem_bytes_power_of_two);

int
snappy_get_uncompressed_length(const char *start, size_t n, size_t *result);

int
snappy_decompress(const char *src, size_t src_len, char *dst, size_t dst_len);

#endif
