#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995, 1998, 2001, 2002 by Ralf Baechle
#

#
# Some DECstations need all possible sections of an ECOFF executable
#
ifdef CONFIG_MACH_DECSTATION
  E2EFLAGS = -a
else
  E2EFLAGS =
endif

#
# Drop some uninteresting sections in the kernel.
# This is only relevant for ELF kernels but doesn't hurt a.out
#
drop-sections	= .reginfo .mdebug .comment .note .pdr
strip-flags	= $(addprefix --remove-section=,$(drop-sections))

all: vmlinux.ecoff vmlinux.srec addinitrd

vmlinux.ecoff:	$(obj)/elf2ecoff vmlinux
	$(obj)/elf2ecoff vmlinux vmlinux.ecoff $(E2EFLAGS)

$(obj)/elf2ecoff: $(obj)/elf2ecoff.c
	$(HOSTCC) -o $@ $^

vmlinux.srec:   vmlinux
	$(OBJCOPY) -S -O srec $(strip-flags) vmlinux $(obj)/vmlinux.srec

$(obj)/addinitrd: $(obj)/addinitrd.c
	$(HOSTCC) -o $@ $^

archhelp:
	@echo	'* vmlinux.ecoff	- ECOFF boot image'

clean-files += addinitrd \
	       elf2ecoff \
	       vmlinux.ecoff \
	       vmlinux.srec \
	       zImage.tmp \
	       zImage
