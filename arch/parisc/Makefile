#
# parisc/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Portions Copyright (C) 1999 The Puffin Group
#
# Modified for PA-RISC Linux by Paul Lahaie, Alex deVries, 
# Mike Shaver, Helge Deller and Martin K. Petersen
#

ifdef CONFIG_PARISC64
CROSS_COMPILE	:= hppa64-linux-
UTS_MACHINE	:= parisc64
LDFLAGS_BLOB	:= --format binary --oformat elf64-hppa-linux
else
MACHINE := $(subst 64,,$(shell uname -m))
ifneq ($(MACHINE),parisc)
CROSS_COMPILE	:= hppa-linux-
endif
LDFLAGS_BLOB	:= --format binary --oformat elf32-hppa-linux
endif

FINAL_LD=$(CROSS_COMPILE)ld --warn-common --warn-section-align 

OBJCOPY_FLAGS =-O binary -R .note -R .comment -S

cflags-y	:= -D__linux__ -pipe -fno-strength-reduce

# These should be on for older toolchains or SOM toolchains that don't
# enable them by default.
cflags-y	+= -mno-space-regs -mfast-indirect-calls

# No fixed-point multiply
cflags-y	+= -mdisable-fpregs

# Without this, "ld -r" results in .text sections that are too big
# (> 0x40000) for branches to reach stubs.
cflags-y	+= -ffunction-sections

# select which processor to optimise for
cflags-$(CONFIG_PA7100)		+= -march=1.1 -mschedule=7100
cflags-$(CONFIG_PA7200)		+= -march=1.1 -mschedule=7200
cflags-$(CONFIG_PA7100LC)	+= -march=1.1 -mschedule=7100LC
cflags-$(CONFIG_PA8X00)		+= -march=2.0 -mschedule=8000

head-y			:= arch/parisc/kernel/head.o 
head-$(CONFIG_PARISC64)	:= arch/parisc/kernel/head64.o

CFLAGS	+= $(cflags-y)
HEAD	:= $(head-y)

core-y	+= $(addprefix arch/parisc/, kernel/pdc_cons.o kernel/process.o   \
		mm/ kernel/ hpux/ math-emu/ kernel/init_task.o )
libs-y	+= arch/parisc/lib/ `$(CC) -print-libgcc-file-name`

drivers-$(CONFIG_OPROFILE)		+= arch/parisc/oprofile/

palo: vmlinux
	@if [ $$(palo -f /dev/null >/dev/null 2>&1 ; echo $$?) != 2 ]; then \
		echo 'ERROR: Please install palo first (apt-get install palo)';\
		echo 'or build it from source and install it somewhere in your $$PATH';\
		false; \
	fi
	@if [ ! -f ./palo.conf ]; then \
		cp arch/parisc/defpalo.conf palo.conf; \
		echo 'A generic palo config file (./palo.conf) has been created for you.'; \
		echo 'You should check it and re-run "make palo".'; \
		echo 'WARNING: the "lifimage" file is now placed in this directory by default!'; \
		false; \
	fi
	palo -f ./palo.conf

oldpalo: vmlinux
	export TOPDIR=`pwd`; \
	unset STRIP LDFLAGS CPP CPPFLAGS AFLAGS CFLAGS CC LD; cd ../palo && make lifimage

# Shorthands for known targets not supported by parisc, use palo as default
Image zImage bzImage: palo

prepare: include/asm-parisc/offsets.h

arch/parisc/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-parisc/offsets.h: arch/parisc/kernel/asm-offsets.s
	@echo -n '  Generating $@'
	@$(generate-asm-offsets.h) < $< > $@.tmp
	@$(update-if-changed)

archclean:
archmrproper:

CLEAN_FILES	+= palo.conf lifimage include/asm-parisc/offsets.h \
		include/asm-parisc/offsets.h.tmp

define archhelp
	@echo  '* vmlinux	- Uncompressed kernel image (./vmlinux)'
	@echo  '  palo		- Bootable image (./lifimage)'
endef
