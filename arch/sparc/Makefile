# $Id: Makefile,v 1.49 2001/07/27 09:42:22 davem Exp $
# sparc/Makefile
#
# Makefile for the architecture dependent flags and dependencies on the
# Sparc.
#
# Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
#

# If the solaris /bin/sh wasn't so broken, I wouldn't need the following
# line...
SHELL  =/bin/bash

#
# Uncomment the first CFLAGS if you are doing kgdb source level
# debugging of the kernel to get the proper debugging information.

IS_EGCS := $(shell if $(CC) -m32 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo y; else echo n; fi; )
NEW_GAS := $(shell if $(LD) --version 2>&1 | grep 'elf64_sparc' > /dev/null; then echo y; else echo n; fi)

ifeq ($(NEW_GAS),y)
AS              := $(AS) -32
LDFLAGS		:= -m elf32_sparc
endif

#CFLAGS := $(CFLAGS) -g -pipe -fcall-used-g5 -fcall-used-g7
ifneq ($(IS_EGCS),y)
CFLAGS := $(CFLAGS) -pipe -mno-fpu -fcall-used-g5 -fcall-used-g7
else
CFLAGS := $(CFLAGS) -m32 -pipe -mno-fpu -fcall-used-g5 -fcall-used-g7
endif

#LDFLAGS_vmlinux = -N -Ttext 0xf0004000
#  Since 2.5.40, the first stage is left not btfix-ed.
#  Actual linking is done with "make image".
LDFLAGS_vmlinux = -r

HEAD := arch/sparc/kernel/head.o arch/sparc/kernel/init_task.o

core-y += arch/sparc/kernel/ arch/sparc/mm/ arch/sparc/math-emu/
libs-y += arch/sparc/prom/ arch/sparc/lib/

# Export what is needed by arch/sparc/boot/Makefile
export CORE_FILES
export INIT

image: vmlinux
	$(MAKE) -C arch/sparc/boot image

archclean:
	rm -f $(TOPDIR)/vmlinux.aout
	-$(MAKE) -C arch/sparc/boot clean

archmrproper:
	rm -f $(TOPDIR)/include/asm-sparc/asm_offsets.h

prepare: check_asm

check_asm: include/linux/version.h include/asm include/config/MARKER
	$(MAKE) -C arch/sparc/kernel check_asm

tftpboot.img:
	$(MAKE) -C arch/sparc/boot tftpboot.img
