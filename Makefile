CC := gcc

PROGRAM = glibcrun
CFLAGS  = -O2 -std=c99 -Wall -W
LDFLAGS =

INSTALL = /usr/bin/env install
PREFIX	= /usr/local

OBJS = glibcrun.o

all: $(PROGRAM)

$(PROGRAM): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

install: $(PROGRAM)
	$(INSTALL) $(PROGRAM) $(PREFIX)/bin
	chmod u+s $(PREFIX)/bin/${PROGRAM}

clean:
	rm -f $(OBJS) $(PROGRAM)

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -I. -o $@

.PHONY: all install clean distclean
