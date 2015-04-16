CC ?= clang

CFLAGS = -I../src -Wall

test_uriparser: test_uriparser.o ../src/uriparser.o
	$(CC) -o $@ $?

test_uriparser.o: test_uriparser.c ../src/uriparser.h
	$(CC) $(CFLAGS) -c test_uriparser.c

clean:
	$(RM) *.o test_uriparser
