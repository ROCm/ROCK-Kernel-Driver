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
		-O elf32-tradlittlemips \
		$(strip-flags) \
		$< $@

vmlinux.ecoff:	$(obj)/elf2ecoff vmlinux
	./elf2ecoff vmlinux $(obj)/vmlinux.ecoff $(E2EFLAGS)

$(obj)/elf2ecoff: $(obj)/elf2ecoff.c
	$(HOSTCC) -o $@ $^

$(obj)/addinitrd: $(obj)/addinitrd.c
	$(HOSTCC) -o $@ $^

archhelp:
	@echo	'* vmlinux.rm200	- Bootable kernel image for RM200C'

CLEAN_FILES += addinitrd \
	       elf2ecoff \
	       vmlinux.ecoff \
	       vmlinux.rm200 \
	       zImage.tmp \
	       zImage
