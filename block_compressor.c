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
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <lzo/lzo1x.h>
#include "csnappy.h"
#include <zlib.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

static int PAGE_SIZE, PAGE_SHIFT;

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

static void* noop(void) { return NULL; }
static void noop_p(void *p) { }

static void* lzo_compress_init(void)
{
	char *workmem;
	if (!(workmem = malloc(LZO1X_1_MEM_COMPRESS)))
		handle_error("malloc");
	return workmem;
}

static void lzo_compress_free(void *opaque)
{
	free(opaque);
}

static void lzo_compress(
	const char *src,
	uint32_t ilen,
	char *dst,
	uint32_t *dst_len,
	void *opaque)
{
	lzo_uint olen;
	lzo1x_1_compress((unsigned char *)src, ilen,
			 (unsigned char *)dst, &olen, opaque);
	*dst_len = olen;
}

static int lzo_decompress(
	const char *src,
	uint32_t ilen,
	char *dst,
	uint32_t *dst_len,
	void *opaque)
{
	lzo_uint olen = *dst_len;
	int ret;
	ret = lzo1x_decompress_safe(
		(unsigned char *)src, ilen,
		(unsigned char *)dst, &olen, NULL);
	*dst_len = olen;
	return ret;
}


static void* snappy_compress_init(void)
{
	char *workmem;
	if (!(workmem = malloc(CSNAPPY_WORKMEM_BYTES)))
		handle_error("malloc");
	return workmem;
}

static void snappy_compress_free(void *opaque)
{
	free(opaque);
}

static void snappy_compress(
	const char *src,
	uint32_t ilen,
	char *dst,
	uint32_t *dst_len,
	void *opaque)
{
	char *end;
	end = csnappy_compress_fragment(src, ilen, dst,
		opaque, CSNAPPY_WORKMEM_BYTES_POWER_OF_TWO);
	*dst_len = end - dst;
}

static int snappy_decompress(
	const char *src,
	uint32_t ilen,
	char *dst,
	uint32_t *dst_len,
	void *opaque)
{
	return csnappy_decompress_noheader(src, ilen, dst, dst_len);
}


static void* zlib_compress_init(void)
{
	z_stream *zs;
	if (!(zs = malloc(sizeof(z_stream))))
		handle_error("malloc");
	zs->zalloc = Z_NULL;
	zs->zfree = Z_NULL;
	zs->opaque = Z_NULL;
	if (deflateInit(zs, Z_DEFAULT_COMPRESSION) != Z_OK)
		handle_error("deflateInit");
	return zs;
}

static void zlib_compress_free(void *opaque)
{
	if (deflateEnd(opaque) != Z_OK)
		handle_error("deflateEnd");
	free(opaque);
}

static void zlib_compress(
	const char *src,
	uint32_t ilen,
	char *dst,
	uint32_t *dst_len,
	void *opaque)
{
	z_stream *zs = opaque;
	zs->avail_in = ilen;
	zs->next_in = (unsigned char *)src;
	zs->avail_out = *dst_len;
	zs->next_out = (unsigned char *)dst;
	if (deflate(zs, Z_FINISH) != Z_STREAM_END)
		handle_error("deflate");
	if (deflateReset(zs) != Z_OK)
		handle_error("deflateReset");
	*dst_len = *dst_len - zs->avail_out;
}

static void* zlib_decompress_init(void)
{
	z_stream *zs;
	if (!(zs = malloc(sizeof(z_stream))))
		handle_error("malloc");
	zs->zalloc = Z_NULL;
	zs->zfree = Z_NULL;
	zs->opaque = Z_NULL;
	zs->next_in = Z_NULL;
	zs->avail_in = 0;
	if (inflateInit(zs) != Z_OK)
		handle_error("inflateInit");
	return zs;
}

static void zlib_decompress_free(void *opaque)
{
	if (inflateEnd(opaque) != Z_OK)
		handle_error("inflateEnd");
	free(opaque);
}

static int zlib_decompress(
	const char *src,
	uint32_t ilen,
	char *dst,
	uint32_t *dst_len,
	void *opaque)
{
	z_stream *zs = opaque;
	zs->avail_in = ilen;
	zs->next_in = (unsigned char *)src;
	zs->avail_out = *dst_len;
	zs->next_out = (unsigned char *)dst;
	if (inflate(zs, Z_FINISH) != Z_STREAM_END)
		handle_error("inflate");
	if (inflateReset(zs) != Z_OK)
		handle_error("inflateReset");
	*dst_len = *dst_len - zs->avail_out;
	return 0;
}


enum {
	LZO = 0,
	SNAPPY = 1,
	ZLIB = 2,
};

static const char* const COMPRESSORS[] = { "LZO", "SNAPPY", "ZLIB" };

typedef void (*compress_fn)(const char *src, uint32_t ilen, char *dst,
				uint32_t *dst_len, void *opaque);

typedef int (*decompress_fn)(const char *src, uint32_t ilen, char *dst,
				uint32_t *dst_len, void *opaque);

struct compressor_funcs {
	void* (*compress_init)(void);
	void (*compress_free)(void *opaque);
	compress_fn compress;
	void* (*decompress_init)(void);
	void (*decompress_free)(void *opaque);
	decompress_fn decompress;
};

static const struct compressor_funcs compressors[] = {
	{lzo_compress_init, lzo_compress_free, lzo_compress,
		noop, noop_p, lzo_decompress},
	{snappy_compress_init, snappy_compress_free, snappy_compress,
		noop, noop_p, snappy_decompress},
	{zlib_compress_init, zlib_compress_free, zlib_compress,
		zlib_decompress_init, zlib_decompress_free, zlib_decompress},
};

#define ONE_BILLION 1000000000
static void add_time_diff(struct timespec *total,
			struct timespec *start,
			struct timespec *end)
{
	end->tv_sec -= start->tv_sec;
	end->tv_nsec -= start->tv_nsec;
	if (end->tv_nsec < 0) {
		end->tv_sec--;
		end->tv_nsec += ONE_BILLION;
	}
	total->tv_sec += end->tv_sec;
	total->tv_nsec += end->tv_nsec;
	if (total->tv_nsec > ONE_BILLION) {
		total->tv_sec++;
		total->tv_nsec -= ONE_BILLION;
	}
}

union intbytes {
	uint32_t i;
	char c[4];
};

static int do_compress(int method, FILE *ifile, FILE *ofile)
{
	union intbytes intbuf;
	char *ibuf, *obuf, *opaque;
	compress_fn compress = compressors[method].compress;
	uint32_t counts[3] = { 0 };
	struct timespec t1, t2, elapsed;
	memset(&elapsed, 0, sizeof(elapsed));
	if (!(ibuf = malloc(PAGE_SIZE)))
		handle_error("malloc");
	if (!(obuf = malloc(2 * PAGE_SIZE)))
		handle_error("malloc");
	opaque = compressors[method].compress_init();
	if (fseek(ifile, 0, SEEK_END) == -1)
		handle_error("fseek");
	long input_length = ftell(ifile);
	if (fseek(ifile, 0, SEEK_SET) == -1)
		handle_error("fseek");
	long nr_pages = DIV_ROUND_UP(input_length, PAGE_SIZE);
	if (nr_pages > UINT32_MAX)
		handle_error("inut file too big");
	printf("compressor: %s\n", COMPRESSORS[method]);
	printf("#pages: %u\n", (unsigned)nr_pages);
	intbuf.i = (uint32_t)nr_pages;
	if (fwrite(&intbuf.c, 1, 4, ofile) < 4)
		handle_error("fwrite");
	/* expand ofile to place of first compressed block data */
	fseek(ofile, nr_pages * sizeof(uint32_t), SEEK_SET);
	/* write something so the file will grow. end of file now points to
	 * start of compressed data of first block */
	if (fwrite(&intbuf, 1, 4, ofile) < 4)
		handle_error("fwrite");
	for (uint32_t i = 0; i < nr_pages; i++) {
		uint32_t ilen = fread(ibuf, 1, PAGE_SIZE, ifile);
		if (ilen < PAGE_SIZE && !feof(ifile))
			handle_error("fread");
		uint32_t olen = 2 * PAGE_SIZE;
		clock_gettime(CLOCK_MONOTONIC, &t1);
		compress(ibuf, ilen, obuf, &olen, opaque);
		clock_gettime(CLOCK_MONOTONIC, &t2);
		char *wbuf = obuf;
		if (olen >= ilen) {
			olen = ilen;
			wbuf = ibuf;
			counts[2]++;
		} else if (olen > (PAGE_SIZE / 2)) {
			counts[1]++;
		} else {
			counts[0]++;
		}
		if (fseek(ofile, (i + 1) * sizeof(uint32_t), SEEK_SET) == -1)
			handle_error("fseek");
		intbuf.i = olen;
		if (fwrite(&intbuf.c, 1, 4, ofile) < 4)
			handle_error("fwrite");
		if (fseek(ofile, 0, SEEK_END) == -1)
			handle_error("fseek");
		if (fwrite(wbuf, 1, olen, ofile) < olen)
			handle_error("fwrite");
		add_time_diff(&elapsed, &t1, &t2);
	}
	fclose(ofile);
	fclose(ifile);
	free(obuf);
	free(ibuf);
	compressors[method].compress_free(opaque);
	printf("> 100%%\t:%u\n> 50%%\t:%u\n<= 50%%\t:%u\n"
		"%d.%09ld seconds\n",
		counts[2],counts[1],counts[0],
		(int)elapsed.tv_sec, elapsed.tv_nsec);
	return 0;
}

static int do_decompress(int method, FILE *ifile, FILE *ofile)
{
	union intbytes intbuf;
	char *ibuf, *obuf, *opaque;
	decompress_fn decompress = compressors[method].decompress;
	uint64_t ipos;
	uint32_t nr_pages;
	if (!(ibuf = malloc(2 * PAGE_SIZE)))
		handle_error("malloc");
	if (!(obuf = malloc(PAGE_SIZE)))
		handle_error("malloc");
	opaque = compressors[method].decompress_init();
	if (fread(&intbuf.c, 1, 4, ifile) < 4)
		handle_error("fread");
	nr_pages = intbuf.i;
	printf("nr_pages: %u\n", nr_pages);
	ipos = (nr_pages + 1) * sizeof(uint32_t);
	for (uint32_t i = 0; i < nr_pages; i++) {
		if (fseek(ifile, (i + 1) * sizeof(uint32_t), SEEK_SET) == -1)
			handle_error("fseek");
		if (fread(&intbuf.c, 1, 4, ifile) < 4)
			handle_error("fread");
		uint32_t ilen = intbuf.i;
		if (fseek(ifile, ipos, SEEK_SET) == -1)
			handle_error("fseek");
		if (fread(ibuf, 1, ilen, ifile) < ilen)
			handle_error("fread");
		ipos += ilen;
		uint32_t olen = PAGE_SIZE;
		char *wbuf = obuf;
		if (ilen == PAGE_SIZE) {
			wbuf = ibuf;
		} else {
			if (decompress(ibuf, ilen, obuf, &olen, opaque))
				handle_error("decompress");
		}
		if (fwrite(wbuf, 1, olen, ofile) < olen)
			handle_error("fwrite");
		printf("%d -> %d\n", ilen, olen);
	}
	fclose(ofile);
	fclose(ifile);
	free(obuf);
	free(ibuf);
	compressors[method].decompress_free(opaque);
	return 0;
}

int main(int argc, char * const argv[])
{
	int c, compressor = -1, decompress = 0;
	const char *ifile_name, *ofile_name;
	FILE *ifile, *ofile;

	while((c = getopt(argc, argv, "c:d")) != -1) {
		switch (c) {
		case 'c':
			if (strcasecmp(optarg, COMPRESSORS[LZO]) == 0)
				compressor = LZO;
			else if (strcasecmp(optarg, COMPRESSORS[SNAPPY]) == 0)
				compressor = SNAPPY;
			else if (strcasecmp(optarg, COMPRESSORS[ZLIB]) == 0)
				compressor = ZLIB;
			else
				goto usage;
			break;
		case 'd':
			decompress = 1;
			break;
		default:
			goto usage;
		}
	}
	if (optind > argc - 2)
		goto usage;
	ifile_name = argv[optind];
	ofile_name = argv[optind + 1];
	if (!(ifile = fopen(ifile_name, "rb"))) {
		perror("fopen of ifile_name");
		return 2;
	}
	if (!(ofile = fopen(ofile_name, "wb"))) {
		perror("fopen of ofile_name");
		return 3;
	}
	PAGE_SIZE = (int)sysconf(_SC_PAGE_SIZE);
	PAGE_SHIFT = ffs(PAGE_SIZE) - 1;
	if (!decompress)
		return do_compress(compressor, ifile, ofile);
	else
		return do_decompress(compressor, ifile, ofile);
usage:
	fprintf(stderr,
		"usage: block_compressor -c lzo|snappy|zlib [-d] ifile ofile\n");
	return 1;
}
