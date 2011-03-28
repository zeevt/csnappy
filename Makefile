
OPT_FLAGS = -g -O2 -DNDEBUG
DBG_FLAGS = -ggdb -O0 -DDEBUG
CFLAGS = -std=c99 -Wall -pedantic -D__LITTLE_ENDIAN
LDFLAGS="-Wl,-O1"

all: test

csnappy_decompress: csnappy_decompress.c csnappy_internal.h
	$(CC) $(CFLAGS) $(OPT_FLAGS) -DTEST -o $@ $<

csnappy_decompress_debug: csnappy_decompress.c csnappy_internal.h
	$(CC) $(CFLAGS) $(DBG_FLAGS) -DTEST -o $@ $<

csnappy_compress: csnappy_compress.c csnappy_internal.h
	$(CC) $(CFLAGS) $(OPT_FLAGS) -DTEST -o $@ $<

csnappy_compress_debug: csnappy_compress.c csnappy_internal.h
	$(CC) $(CFLAGS) $(DBG_FLAGS) -DTEST -o $@ $<

test: csnappy_decompress csnappy_compress
	rm -f urls.10K && ./csnappy_decompress testdata/urls.10K.snappy urls.10K && diff -u testdata/urls.10K urls.10K && echo "decompress OK"
	rm -f urls.10K.snappy && ./csnappy_compress testdata/urls.10K urls.10K.snappy && diff -u testdata/urls.10K.snappy urls.10K.snappy && echo "compress OK"

check_leaks: csnappy_decompress csnappy_compress
	valgrind --leak-check=full --show-reachable=yes ./csnappy_decompress testdata/urls.10K.snappy urls.10K
	valgrind --leak-check=full --show-reachable=yes ./csnappy_decompress testdata/baddata3.snappy baddata3 || true
	valgrind --leak-check=full --show-reachable=yes ./csnappy_compress testdata/urls.10K urls.10K.snappy

clean:
	rm -f *.o *_debug csnappy_compress csnappy_decompress
