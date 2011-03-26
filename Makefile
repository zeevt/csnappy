
all: test

csnappy_decompress: csnappy_decompress.c csnappy_internal.h
	gcc -std=c99 -Wall -pedantic -g -O2 -fwhole-program -DTEST -o $@ $<

csnappy_decompress_debug: csnappy_decompress.c csnappy_internal.h
	gcc -std=c99 -Wall -pedantic -g -O2 -fwhole-program -DTEST -DDEBUG -o $@ $<

test: csnappy_decompress
	rm -f urls.10K
	./csnappy_decompress testdata/urls.10K.snappy urls.10K
	diff -u testdata/urls.10K urls.10K

check_leaks: csnappy_decompress
	valgrind --leak-check=full --show-reachable=yes ./csnappy_decompress testdata/urls.10K.snappy urls.10K
	valgrind --leak-check=full --show-reachable=yes ./csnappy_decompress testdata/baddata3.snappy baddata3

clean:
	rm -f *.o csnappy_decompress
