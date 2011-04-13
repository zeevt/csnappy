
OPT_FLAGS = -g -O2 -DNDEBUG -fomit-frame-pointer
DBG_FLAGS = -ggdb -O0 -DDEBUG
CFLAGS := -std=gnu89 -Wall -pedantic -D__LITTLE_ENDIAN -DHAVE_BUILTIN_CTZ
ifeq (${DEBUG},yes)
CFLAGS += $(DBG_FLAGS)
else
CFLAGS += $(OPT_FLAGS)
endif
LDFLAGS = -Wl,-O1

all: cl_test check_leaks

cl_tester: cl_tester.c csnappy.h libcsnappy.so
	$(CC) $(CFLAGS) $(LDFLAGS) -D_GNU_SOURCE -o $@ $< libcsnappy.so

cl_test: cl_tester
	export LD_LIBRARY_PATH=.
	rm -f afifo
	mkfifo afifo
	./cl_tester -c <testdata/urls.10K | ./cl_tester -d -c > afifo &
	diff testdata/urls.10K afifo && echo "compress-decompress restores original"
	rm -f afifo
	./cl_tester -S d && echo "decompression is safe"
	./cl_tester -S c || echo "compression overwrites out buffer"

check_leaks: cl_tester
	valgrind --leak-check=full --show-reachable=yes ./cl_tester -d -c <testdata/urls.10K.snappy >/dev/null
	valgrind --leak-check=full --show-reachable=yes ./cl_tester -d -c <testdata/baddata3.snappy >/dev/null || true
	valgrind --leak-check=full --show-reachable=yes ./cl_tester -c <testdata/urls.10K >/dev/null
	valgrind --leak-check=full --show-reachable=yes ./cl_tester -S d

libcsnappy.so: csnappy_compress.c csnappy_decompress.c csnappy_internal.h csnappy_internal_userspace.h
	$(CC) $(CFLAGS) -fPIC -DPIC -c -o csnappy_compress.o csnappy_compress.c
	$(CC) $(CFLAGS) -fPIC -DPIC -c -o csnappy_decompress.o csnappy_decompress.c
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ csnappy_compress.o csnappy_decompress.o

cl_tester_simple: cl_tester.c csnappy_simple.c libcsnappy.so
	$(CC) -std=c99 -Wall -pedantic -O2 -g -c -o csnappy_simple.o csnappy_simple.c
	$(CC) -std=c99 -Wall -pedantic -O2 -g -D_GNU_SOURCE -c -o cl_tester.o cl_tester.c
	$(CC) $(LDFLAGS) -o cl_tester cl_tester.o csnappy_simple.o libcsnappy.so

libcsnappy_simple: csnappy_compress.c csnappy_internal.h csnappy_internal_userspace.h
	$(CC) $(CFLAGS) -fPIC -DPIC -c -o csnappy_compress.o csnappy_compress.c
	$(CC) -std=c99 -Wall -pedantic -O2 -g -fPIC -DPIC -c -o csnappy_simple.o csnappy_simple.c
	$(CC) $(LDFLAGS) -shared -o libcsnappy.so csnappy_compress.o csnappy_simple.o

install: csnappy.h libcsnappy.so
	cp csnappy.h /usr/include/
	cp libcsnappy.so /usr/lib/

uninstall:
	rm -f /usr/include/csnappy.h
	rm -f /usr/lib/libcsnappy.so

clean:
	rm -f *.o *_debug libcsnappy.so cl_tester
