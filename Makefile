CC := gcc

CFLAGS  = -O2 -std=c99 -Wall -W
LDFLAGS =

INSTALL = /usr/bin/env install
PREFIX	= /usr/local

test: testserver testclient

run: voidnsrun.o utils.o
	$(CC) $(CFLAGS) -o voidnsrun $^ $(LDFLAGS)

undo: voidnsundo.o utils.o
	$(CC) $(CFLAGS) -o voidnsundo $^ $(LDFLAGS)

testserver: test/testserver.o utils.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

testclient: test/testclient.o utils.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

install-run: run
	$(INSTALL) voidnsrun $(PREFIX)/bin
	chmod u+s $(PREFIX)/bin/voidnsrun

install-undo: undo
	$(INSTALL) voidnsundo $(PREFIX)/bin
	chmod u+s $(PREFIX)/bin/voidnsundo

clean:
	rm -f *.o test/*.o voidnsrun voidnsundo testserver testclient

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -I. -o $@

.PHONY: run undo install-run install-undo clean