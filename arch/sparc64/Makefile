# $Id: Makefile,v 1.51 2001/11/17 00:15:27 davem Exp $
# sparc64/Makefile
#
# Makefile for the architecture dependent flags and dependencies on the
# 64-bit Sparc.
#
# Copyright (C) 1996,1998 David S. Miller (davem@caip.rutgers.edu)
# Copyright (C) 1998 Jakub Jelinek (jj@ultra.linux.cz)
#

# If the solaris /bin/sh wasn't so broken, I wouldn't need the following
# line...
SHELL  =/bin/bash

CC		:= $(shell if gcc -m64 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo gcc; else echo sparc64-linux-gcc; fi )

NEW_GCC := $(shell if $(CC) -m64 -mcmodel=medlow -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo y; else echo n; fi; )
NEW_GAS := $(shell if $(LD) -V 2>&1 | grep 'elf64_sparc' > /dev/null; then echo y; else echo n; fi)
UNDECLARED_REGS := $(shell if $(CC) -c -x assembler /dev/null -Wa,--help | grep undeclared-regs > /dev/null; then echo y; else echo n; fi; )

export NEW_GCC

ifneq ($(NEW_GAS),y)
AS		= sparc64-linux-as
LD		= sparc64-linux-ld
NM		= sparc64-linux-nm
AR		= sparc64-linux-ar
RANLIB		= sparc64-linux-ranlib
else
AS		:= $(AS) -64
LD		:= $(LD) -m elf64_sparc
endif
ELFTOAOUT	= elftoaout
ifneq ($(UNDECLARED_REGS),y)
CC_UNDECL	=
else
CC_UNDECL	= -Wa,--undeclared-regs
AS		:= $(AS) --undeclared-regs
endif

ifneq ($(NEW_GCC),y)
  CFLAGS := $(CFLAGS) -pipe -mno-fpu -mtune=ultrasparc -mmedlow \
	    -ffixed-g4 -fcall-used-g5 -fcall-used-g7 -Wno-sign-compare
else
  CFLAGS := $(CFLAGS) -m64 -pipe -mno-fpu -mcpu=ultrasparc -mcmodel=medlow \
	    -ffixed-g4 -fcall-used-g5 -fcall-used-g7 -Wno-sign-compare \
	    $(CC_UNDECL)
  AFLAGS += -m64 -mcpu=ultrasparc $(CC_UNDECL)
endif

LINKFLAGS = -T arch/sparc64/vmlinux.lds

HEAD := arch/sparc64/kernel/head.o arch/sparc64/kernel/init_task.o

SUBDIRS += arch/sparc64/kernel arch/sparc64/lib arch/sparc64/mm \
	arch/sparc64/prom

ifneq ($(CONFIG_SOLARIS_EMUL),n)
  SUBDIRS += arch/sparc64/solaris
endif

SUBDIRS += arch/sparc64/math-emu

CORE_FILES := arch/sparc64/kernel/kernel.o arch/sparc64/mm/mm.o $(CORE_FILES)

ifeq ($(CONFIG_SOLARIS_EMUL),y)
  CORE_FILES += arch/sparc64/solaris/solaris.o
endif

CORE_FILES += arch/sparc64/math-emu/math-emu.o

LIBS := $(TOPDIR)/lib/lib.a $(LIBS) $(TOPDIR)/arch/sparc64/prom/promlib.a \
	$(TOPDIR)/arch/sparc64/lib/lib.a

vmlinux.aout: vmlinux
	$(ELFTOAOUT) -o $(TOPDIR)/vmlinux.aout $(TOPDIR)/vmlinux

archclean:
	rm -f $(TOPDIR)/vmlinux.aout

archmrproper:
	rm -f $(TOPDIR)/include/asm-sparc64/asm_offsets.h

archdep: check_asm

check_asm: include/linux/version.h
	$(MAKE) -C arch/sparc64/kernel check_asm

tftpboot.img:
	$(MAKE) -C arch/sparc64/boot tftpboot.img
