CC = gcc
CFLAGS = -W -Wall -O2 -g
CPPFLAGS = -I../../include
LDLIBS = -lz
PROGS = mkcramfs cramfsck

all: $(PROGS)

distclean clean:
	rm -f $(PROGS)

.PHONY: all clean
