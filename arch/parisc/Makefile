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
CROSS_COMPILE := hppa64-linux-
UTS_MACHINE := parisc64
LDFLAGS_BLOB := --format binary --oformat elf64-hppa-linux

else
MACHINE	:= $(subst 64,,$(shell uname -m))
ifneq (${MACHINE},parisc)
# cross compilation
CROSS_COMPILE := hppa-linux-
endif
LDFLAGS_BLOB := --format binary --oformat elf32-hppa-linux
endif

FINAL_LD=$(CROSS_COMPILE)ld --warn-common --warn-section-align 

OBJCOPY_FLAGS =-O binary -R .note -R .comment -S

CFLAGS_PIPE := -pipe
CFLAGS_NSR  := -fno-strength-reduce
CFLAGS := $(CFLAGS) -D__linux__ $(CFLAGS_PIPE) $(CFLAGS_NSR)

# These should be on for older toolchains or SOM toolchains that don't
# enable them by default.
CFLAGS += -mno-space-regs -mfast-indirect-calls

# No fixed-point multiply
CFLAGS += -mdisable-fpregs

# Without this, "ld -r" results in .text sections that are too big
# (> 0x40000) for branches to reach stubs.
CFLAGS += -ffunction-sections

# select which processor to optimise for
ifdef CONFIG_PA7100
CFLAGS += -march=1.1 -mschedule=7100
endif

ifdef CONFIG_PA7200
CFLAGS += -march=1.1 -mschedule=7200
endif

ifdef CONFIG_PA7100LC
CFLAGS += -march=1.1 -mschedule=7100LC
endif

ifdef CONFIG_PA8X00
CFLAGS += -march=2.0 -mschedule=8000
endif

HEAD := arch/parisc/kernel/head.o 
ifdef CONFIG_PARISC64
HEAD := arch/parisc/kernel/head64.o
endif

core-y					+= arch/parisc/kernel/pdc_cons.o  \
					   arch/parisc/kernel/process.o   \
	   				   arch/parisc/mm/	          \
					   arch/parisc/kernel/            \
					   arch/parisc/hpux/		  \
					   arch/parisc/math-emu/	  \
					   arch/parisc/kernel/init_task.o
libs-y					+= arch/parisc/lib/ \
					   `$(CC) -print-libgcc-file-name`

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

Image: palo

Image-clean:

ramdisk.o:

zImage: palo

bzImage: palo

compressed: zImage

install: 

archmrproper:

archclean:

prepare: include/asm-$(ARCH)/offsets.h

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/offsets.h.tmp: arch/$(ARCH)/kernel/asm-offsets.s
	@$(generate-asm-offsets.h) < $< > $@

include/asm-$(ARCH)/offsets.h: include/asm-$(ARCH)/offsets.h.tmp
	@echo -n '  Generating $@'
	@$(update-if-changed)

