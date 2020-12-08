CC := gcc

CFLAGS  = -O2 -std=c99 -Wall -W
LDFLAGS =

INSTALL = /usr/bin/env install
PREFIX	= /usr/local

all:
	@echo make run: build voidnsrun.
	@echo make install-run: install voidnsrun to $(PREFIX).
	@echo make undo: build voidnsundo.
	@echo make install-undo: install voidnsundo to $(PREFIX).

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

.PHONY: all run undo install-run install-undo clean