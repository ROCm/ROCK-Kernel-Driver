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

FINAL_LD=$(CROSS_COMPILE)ld --warn-common --warn-section-align 

OBJCOPY_FLAGS =-O binary -R .note -R .comment -S

CFLAGS_PIPE := -pipe
CFLAGS_NSR  := -fno-strength-reduce
CFLAGS := $(CFLAGS) -D__linux__ $(CFLAGS_PIPE) $(CFLAGS_NSR)

# These should be on for older toolchains or SOM toolchains that don't
# enable them by default.
CFLAGS += -mno-space-regs -mfast-indirect-calls

# If we become able to compile for specific platforms, this should be
# conditional on that.
CFLAGS += -mschedule=7200

# No fixed-point multiply
CFLAGS += -mdisable-fpregs

HEAD = arch/parisc/kernel/head.o 

SUBDIRS += arch/parisc/tools
core-y					+= arch/parisc/kernel/pdc_cons.o  \
					   arch/parisc/kernel/process.o   \
	   				   arch/parisc/mm/	          \
					   arch/parisc/kernel/            \
					   arch/parisc/hpux/		  \
					   arch/parisc/kernel/init_task.o
libs-y					+= arch/parisc/lib/lib.a          \
					   `$(CC) -print-libgcc-file-name`
drivers-$(CONFIG_MATH_EMULATION)	+= arch/parisc/math-emu/
drivers-$(CONFIG_KWDB)			+= arch/parisc/kdb/

palo: vmlinux
	export TOPDIR=`pwd`; export CONFIG_STI_CONSOLE=$(CONFIG_STI_CONSOLE); \
	unset STRIP LDFLAGS CPP CPPFLAGS AFLAGS CFLAGS CC LD; cd ../palo && make lifimage

Image: palo

Image-clean:

ramdisk.o:

zImage: palo

bzImage: palo

compressed: zImage

install: 

archclean:

archmrproper:
