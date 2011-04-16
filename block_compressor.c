#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <strings.h>
#include <unistd.h>
#include <sys/mman.h>
#include <lzo/lzo1x.h>
#include <csnappy.h>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

enum {
	LZO = 0,
	SNAPPY = 1
};

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

static int do_compress(int method, FILE *ifile, FILE *ofile)
{
	char intbuf[4];
	char *ibuf, *obuf, *workmem;
	long PAGE_SIZE = sysconf(_SC_PAGE_SIZE);
	if (!(ibuf = malloc(PAGE_SIZE)))
		handle_error("malloc");
	if (!(obuf = malloc(2 * PAGE_SIZE)))
		handle_error("malloc");
	int WM_SIZE;
	switch (method) {
	case LZO: WM_SIZE = LZO1X_1_MEM_COMPRESS; break;
	case SNAPPY: WM_SIZE = CSNAPPY_WORKMEM_BYTES; break;
	}
	if (!(workmem = malloc(WM_SIZE)))
		handle_error("malloc");
	if (fseek(ifile, 0, SEEK_END) == -1)
		handle_error("fseek");
	long input_length = ftell(ifile);
	if (fseek(ifile, 0, SEEK_SET) == -1)
		handle_error("fseek");
	long nr_pages = DIV_ROUND_UP(input_length, PAGE_SIZE);
	if (nr_pages > UINT32_MAX)
		handle_error("inut file too big");
	printf("nr_pages: %lu\n", nr_pages);
	*(uint32_t *)intbuf = (uint32_t)nr_pages;
	if (fwrite(intbuf, 1, 4, ofile) < 4)
		handle_error("fwrite");
	/* expand ofile to place of first compressed block data */
	fseek(ofile, nr_pages * sizeof(uint32_t), SEEK_SET);
	/* write something so the file will grow. end of file now points to
	 * start of compressed data of first block */
	if (fwrite(intbuf, 1, 4, ofile) < 4)
		handle_error("fwrite");
	for (uint32_t i = 0; i < nr_pages; i++) {
		uint32_t ilen = fread(ibuf, 1, PAGE_SIZE, ifile);
		if (ilen < PAGE_SIZE && !feof(ifile))
			handle_error("fread");
		uint32_t olen = 0;
		char *end;
		switch (method) {
		case LZO:
			lzo1x_1_compress((unsigned char *)ibuf, ilen,
					 (unsigned char *)obuf, &olen, workmem);
			break;
		case SNAPPY:
			end = csnappy_compress_fragment(ibuf, ilen, obuf,
				workmem, CSNAPPY_WORKMEM_BYTES_POWER_OF_TWO);
			olen = end - obuf;
			break;
		}
		printf("%d -> %d\n", ilen, olen);
		if (fseek(ofile, (i + 1) * sizeof(uint32_t), SEEK_SET) == -1)
			handle_error("fseek");
		*(uint32_t *)intbuf = olen;
		if (fwrite(intbuf, 1, 4, ofile) < 4)
			handle_error("fwrite");
		if (fseek(ofile, 0, SEEK_END) == -1)
			handle_error("fseek");
		if (fwrite(obuf, 1, olen, ofile) < olen)
			handle_error("fwrite");
	}
	fclose(ofile);
	fclose(ifile);
	free(workmem);
	free(obuf);
	free(ibuf);
	return 0;
}

static int do_decompress(int method, FILE *ifile, FILE *ofile)
{
	char intbuf[4];
	char *ibuf, *obuf;
	uint64_t ipos;
	uint32_t nr_pages;
	long PAGE_SIZE = sysconf(_SC_PAGE_SIZE);
	if (!(ibuf = malloc(2 * PAGE_SIZE)))
		handle_error("malloc");
	if (!(obuf = malloc(PAGE_SIZE)))
		handle_error("malloc");
	if (fread(intbuf, 1, 4, ifile) < 4)
		handle_error("fread");
	nr_pages = *(uint32_t *)intbuf;
	printf("nr_pages: %u\n", nr_pages);
	ipos = (nr_pages + 1) * sizeof(uint32_t);
	for (uint32_t i = 0; i < nr_pages; i++) {
		if (fseek(ifile, (i + 1) * sizeof(uint32_t), SEEK_SET) == -1)
			handle_error("fseek");
		if (fread(intbuf, 1, 4, ifile) < 4)
			handle_error("fread");
		uint32_t ilen = *(uint32_t *)intbuf;
		if (fseek(ifile, ipos, SEEK_SET) == -1)
			handle_error("fseek");
		if (fread(ibuf, 1, ilen, ifile) < ilen)
			handle_error("fread");
		ipos += ilen;
		uint32_t olen = PAGE_SIZE;
		int ret;
		switch (method) {
		case LZO:
			ret = lzo1x_decompress_safe(
				(unsigned char *)ibuf, ilen,
				(unsigned char *)obuf, &olen, NULL);
			break;
		case SNAPPY:
			ret = csnappy_decompress_noheader(
				ibuf, ilen, obuf, &olen);
			break;
		}
		if (ret)
			handle_error("decompress");
		if (fwrite(obuf, 1, olen, ofile) < olen)
			handle_error("fwrite");
		printf("%d -> %d\n", ilen, olen);
	}
	fclose(ofile);
	fclose(ifile);
	free(obuf);
	free(ibuf);
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
			if (strcasecmp(optarg, "lzo") == 0)
				compressor = LZO;
			else if (strcasecmp(optarg, "snappy") == 0)
				compressor = SNAPPY;
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
	if (!decompress)
		return do_compress(compressor, ifile, ofile);
	else
		return do_decompress(compressor, ifile, ofile);
usage:
	fprintf(stderr, "usage: block_compressor -c lzo|snappy [-d] ifile ofile\n");
	return 1;
}
