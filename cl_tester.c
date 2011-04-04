#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "csnappy.h"

#define MAX_INPUT_SIZE 10 * 1024 * 1024

static int do_decompress(FILE *ifile, FILE *ofile)
{
	char *ibuf, *obuf;
	uint32_t ilen, olen;
	int status;

	if (!(ibuf = (char *)malloc(MAX_INPUT_SIZE))) {
		fprintf(stderr, "malloc failed to allocate %d.\n", MAX_INPUT_SIZE);
		fclose(ifile);
		fclose(ofile);
		return 4;
	}

	ilen = fread(ibuf, 1, MAX_INPUT_SIZE, ifile);
	if (!feof(ifile)) {
		fprintf(stderr, "input was longer than %d, aborting.\n", MAX_INPUT_SIZE);
		free(ibuf);
		fclose(ifile);
		fclose(ofile);
		return 5;
	}
	fclose(ifile);

	if ((status = snappy_get_uncompressed_length(ibuf, ilen, &olen)) != SNAPPY_E_OK) {
		fprintf(stderr, "snappy_get_uncompressed_length returned %d.\n", status);
		free(ibuf);
		fclose(ofile);
		return 6;
	}

	if (!(obuf = (char *)malloc(olen))) {
		fprintf(stderr, "malloc failed to allocate %d.\n", (int)olen);
		free(ibuf);
		fclose(ofile);
		return 4;
	}

	status = snappy_decompress(ibuf, ilen, obuf, olen);
	free(ibuf);
	if (status != SNAPPY_E_OK) {
		fprintf(stderr, "snappy_decompress returned %d.\n", status);
		free(obuf);
		fclose(ofile);
		return 7;
	}

	fwrite(obuf, 1, olen, ofile);
	fclose(ofile);
	free(obuf);
	return 0;
}

static int do_compress(FILE *ifile, FILE *ofile)
{
	char *ibuf, *obuf;
	void *working_memory;
	uint32_t ilen, olen, max_compressed_len;

	if (!(ibuf = (char *)malloc(MAX_INPUT_SIZE))) {
		fprintf(stderr, "malloc failed to allocate %d.\n", MAX_INPUT_SIZE);
		fclose(ifile);
		fclose(ofile);
		return 4;
	}

	ilen = fread(ibuf, 1, MAX_INPUT_SIZE, ifile);
	if (!feof(ifile)) {
		fprintf(stderr, "input was longer than %d, aborting.\n", MAX_INPUT_SIZE);
		free(ibuf);
		fclose(ifile);
		fclose(ofile);
		return 5;
	}
	fclose(ifile);

	max_compressed_len = snappy_max_compressed_length(ilen);
	if (!(obuf = (char*)malloc(max_compressed_len))) {
		fprintf(stderr, "malloc failed to allocate %d bytes.\n", (int)max_compressed_len);
		free(ibuf);
		fclose(ofile);
		return 4;
	}

	if (!(working_memory = malloc(SNAPPY_WORKMEM_BYTES))) {
		fprintf(stderr, "malloc failed to allocate %d bytes.\n", SNAPPY_WORKMEM_BYTES);
		free(ibuf);
		fclose(ofile);
		return 4;
	}

	snappy_compress(ibuf, ilen, obuf, &olen,
			working_memory, SNAPPY_WORKMEM_BYTES_POWER_OF_TWO);
	free(ibuf);
	free(working_memory);

	fwrite(obuf, 1, olen, ofile);
	fclose(ofile);
	free(obuf);
	return 0;
}

int main(int argc, char * const argv[])
{
	int c;
	int decompress = 0, files = 1;
	const char *ifile_name, *ofile_name;
	FILE *ifile, *ofile;

	while((c = getopt(argc, argv, "dc")) != -1) {
		switch (c) {
		case 'd':
			decompress = 1;
			break;
		case 'c':
			files = 0;
			break;
		}
	}
	ifile = stdin;
	ofile = stdout;
	if (files) {
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
	}
	if (decompress)
		return do_decompress(ifile, ofile);
	else
		return do_compress(ifile, ofile);
usage:
	fprintf(stderr, "Usage: cl_tester [-d] infile outfile OR cl_tester [-d] -c\n");
	return 1;
}
