#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995, 1998, 1999 by Ralf Baechle
#

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
drop-sections	= .reginfo .mdebug .comment .note
strip-flags	= $(addprefix --remove-section=,$(drop-sections))

all: vmlinux.ecoff addinitrd

vmlinux.rm200: vmlinux
	$(OBJCOPY) \
		--change-addresses=0xfffffffc \
		-O elf32-littlemips \
		$(strip-flags) \
		$< $@

vmlinux.ecoff:	elf2ecoff $(TOPDIR)/vmlinux
	./elf2ecoff $(TOPDIR)/vmlinux vmlinux.ecoff $(E2EFLAGS)

elf2ecoff: $(TOPDIR)/arch/mips/boot/elf2ecoff.c
	$(HOSTCC) -I$(TOPDIR)/arch/mips/boot -I- -o $@ $^

addinitrd: $(TOPDIR)/arch/mips/boot/addinitrd.c
	$(HOSTCC) -I$(TOPDIR)/arch/mips/boot -I- -o $@ $^

clean:
	rm -f vmlinux.ecoff

mrproper:
	rm -f vmlinux.ecoff addinitrd elf2ecoff
