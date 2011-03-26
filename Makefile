
all: test

csnappy_decompress: csnappy_decompress.c csnappy_internal.h
	gcc -std=c99 -Wall -pedantic -O2 -fwhole-program -DTEST -o $@ $<

csnappy_decompress_debug: csnappy_decompress.c csnappy_internal.h
	gcc -std=c99 -Wall -pedantic -O2 -fwhole-program -DTEST -DDEBUG -o $@ $<

test: csnappy_decompress
	rm -f urls.10K
	./csnappy_decompress testdata/urls.10K.snappy urls.10K
	diff -u testdata/urls.10K urls.10K

clean:
	rm -f *.o csnappy_decompress
