# $Id: Makefile,v 1.9 1999/04/07 18:45:23 harald Exp $
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995, 1998 by Ralf Baechle
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

OBJS  = milo.o a.out.o

#
# Some DECstations need all possible sections of an ECOFF executable
#
ifdef CONFIG_DECSTATION
  E2EFLAGS = -a
else
  E2EFLAGS =
endif

#
# Drop some uninteresting sections in the kernel.
# This is only relevant for ELF kernels but doesn't hurt a.out
#
drop-sections	= .reginfo .mdebug
strip-flags	= $(addprefix --remove-section=,$(drop-sections))

all: vmlinux.ecoff addinitrd

vmlinux.ecoff:	$(CONFIGURE) elf2ecoff $(TOPDIR)/vmlinux
	./elf2ecoff $(TOPDIR)/vmlinux vmlinux.ecoff $(E2EFLAGS)

elf2ecoff: elf2ecoff.c
	$(HOSTCC) -o $@ $^

addinitrd: addinitrd.c
	$(HOSTCC) -o $@ $^

# Don't build dependencies, this may die if $(CC) isn't gcc
dep:

clean:
	rm -f vmlinux.ecoff
	rm -f zImage zImage.tmp mkboot

mrproper:
	rm -f vmlinux.ecoff
	rm -f addinitrd
	rm -f elf2ecoff

zImage:	$(CONFIGURE) mkboot $(TOPDIR)/vmlinux
	$(OBJCOPY) $(strip-flags) $(TOPDIR)/vmlinux zImage.tmp
	./mkboot zImage.tmp zImage
	rm -f zImage.tmp

mkboot: mkboot.c
	$(HOSTCC) -o $@ $^

zdisk: 	zImage
	cp $(TOPDIR)/vmlinux $(TOPDIR)/vm
	$(STRIP) $(TOPDIR)/vm
	gzip -9f $(TOPDIR)/vm

dummy:

include $(TOPDIR)/Rules.make
