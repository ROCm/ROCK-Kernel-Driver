# $Id: Makefile,v 1.10 2000/02/23 08:17:46 jj Exp $
# Makefile for the Sparc boot stuff.
#
# Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
# Copyright (C) 1997,1998 Jakub Jelinek (jj@ultra.linux.cz)

ROOT_IMG	=/usr/src/root.img
ELFTOAOUT	=elftoaout

all: btfix.o

tftpboot.img: piggyback
	$(ELFTOAOUT) $(TOPDIR)/vmlinux -o tftpboot.img
	./piggyback tftpboot.img $(TOPDIR)/System.map $(ROOT_IMG)

piggyback: piggyback.c
	$(HOSTCC) $(HOSTCFLAGS) -o piggyback piggyback.c

btfixupprep: btfixupprep.c
	$(HOSTCC) $(HOSTCFLAGS) -o btfixupprep btfixupprep.c

clean:
	rm -f btfixupprep piggyback tftpboot.img btfix.o btfix.s image

BTOBJS := $(HEAD) $(INIT)
BTLIBS := $(CORE_FILES) $(LIBS) \
	$(DRIVERS) $(NETWORKS)

# Actual linking
image: btfix.o
	$(LD) $(LDFLAGS) -T ../vmlinux.lds.s \
		$(patsubst %,$(TOPDIR)/%,$(BTOBJS)) \
		--start-group \
		$(patsubst %,$(TOPDIR)/%,$(BTLIBS)) \
		btfix.o \
		--end-group -o image

btfix.s: btfixupprep $(TOPDIR)/vmlinux
	$(OBJDUMP) -x $(TOPDIR)/vmlinux | ./btfixupprep > btfix.s

btfix.o: btfix.s
	$(CC) -c -o btfix.o btfix.s

include $(TOPDIR)/Rules.make
