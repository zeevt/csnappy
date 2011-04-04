
OPT_FLAGS = -g -O2 -DNDEBUG
DBG_FLAGS = -ggdb -O0 -DDEBUG
CFLAGS = -std=gnu89 -Wall -pedantic -D__LITTLE_ENDIAN -DHAVE_BUILTIN_CTZ
LDFLAGS = -Wl,-O1

all: cl_test check_leaks

cl_tester: cl_tester.c csnappy.h libcsnappy.so
	$(CC) $(CFLAGS) $(OPT_FLAGS) -o $@ $< -L . -lcsnappy

cl_test: cl_tester
	rm -f urls.10K && ./cl_tester -d testdata/urls.10K.snappy urls.10K && \
	diff -u testdata/urls.10K urls.10K && echo "decompress OK" && rm -f urls.10K
	rm -f urls.10K.snappy && ./cl_tester testdata/urls.10K urls.10K.snappy && \
	diff -u testdata/urls.10K.snappy urls.10K.snappy && echo "compress OK" && rm -f urls.10K.snappy

check_leaks: cl_tester
	valgrind --leak-check=full --show-reachable=yes ./cl_tester -d -c <testdata/urls.10K.snappy >/dev/null
	valgrind --leak-check=full --show-reachable=yes ./cl_tester -d -c <testdata/baddata3.snappy >/dev/null || true
	valgrind --leak-check=full --show-reachable=yes ./cl_tester -c <testdata/urls.10K >/dev/null

libcsnappy.so: csnappy_compress.c csnappy_decompress.c csnappy_internal.h
	$(CC) $(CFLAGS) $(OPT_FLAGS) -fPIC -DPIC -c -o csnappy_compress.o csnappy_compress.c
	$(CC) $(CFLAGS) $(OPT_FLAGS) -fPIC -DPIC -c -o csnappy_decompress.o csnappy_decompress.c
	$(CC) $(CFLAGS) $(OPT_FLAGS) $(LDFLAGS) -shared -o $@ csnappy_compress.o csnappy_decompress.o

install: csnappy.h libcsnappy.so
	cp csnappy.h /usr/include/
	cp libcsnappy.so /usr/lib64/

uninstall:
	rm -f /usr/include/csnappy.h
	rm -f /usr/lib64/libcsnappy.so

clean:
	rm -f *.o *_debug libcsnappy.so cl_tester
