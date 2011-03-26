
all: test

csnappy_decompress: csnappy_decompress.c
	gcc -std=c99 -Wall -pedantic -O2 -fwhole-program -DTEST -o $@ $<

test: csnappy_decompress
	rm -f urls.10K
	./csnappy_decompress ~/snappy-read-only/testdata/urls.10K.comp urls.10K
	diff -u ~/snappy-read-only/testdata/urls.10K urls.10K

clean:
	rm -f *.o csnappy_decompress
