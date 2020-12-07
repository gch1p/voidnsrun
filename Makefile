CC := gcc

CFLAGS  = -O2 -std=c99 -Wall -W
LDFLAGS =

INSTALL = /usr/bin/env install
PREFIX	= /usr/local

all: voidnsrun voidnsundo

test: testserver testclient

voidnsrun: voidnsrun.o utils.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

voidnsundo: voidnsundo.o utils.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

testserver: test/testserver.o utils.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

testclient: test/testclient.o utils.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

install: voidnsrun voidnsundo
	$(INSTALL) voidnsrun voidnsundo $(PREFIX)/bin
	chmod u+s $(PREFIX)/bin/voidnsrun $(PREFIX)/bin/voidnsundo

clean:
	rm -f *.o test/*.o voidnsrun voidnsundo testserver testclient

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -I. -o $@

.PHONY: all install clean distclean