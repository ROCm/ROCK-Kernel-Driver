CFLAGS = -Wall -O2
CPPFLAGS = -I../../fs/cramfs
LDLIBS = -lz
PROGS = mkcramfs

all: $(PROGS)

distclean clean:
	rm -f $(PROGS)

.PHONY: all clean
