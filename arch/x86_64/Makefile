#
# x86_64/Makefile
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
#
# 19990713  Artur Skawina <skawina@geocities.com>
#           Added '-march' and '-mpreferred-stack-boundary' support
# 20000913  Pavel Machek <pavel@suse.cz>
#	    Converted for x86_64 architecture
# 20010105  Andi Kleen, add IA32 compiler.
#
# $Id: Makefile,v 1.28 2001/06/29 17:47:43 aj Exp $


#
# boot system currently needs IA32 tools to link (to be fixed) 
#
# Change this to your i386 compiler/binutils
IA32_PREFIX := /usr/bin/
IA32_CC := $(IA32_PREFIX)gcc -O2 -fomit-frame-pointer -nostdinc -I $(HPATH)
IA32_LD := $(IA32_PREFIX)ld
IA32_AS := $(IA32_PREFIX)gcc -D__ASSEMBLY__ -traditional -c -nostdinc -I $(HPATH) 
IA32_OBJCOPY := $(IA32_PREFIX)objcopy
IA32_CPP := $(IA32_PREFIX)gcc -E
export IA32_CC IA32_LD IA32_AS IA32_OBJCOPY IA32_CPP


LD=$(CROSS_COMPILE)ld -m elf_x86_64
OBJCOPY=$(CROSS_COMPILE)objcopy -O binary -R .note -R .comment -S
LDFLAGS=-e stext
LINKFLAGS =-T $(TOPDIR)/arch/x86_64/vmlinux.lds $(LDFLAGS)

CFLAGS += $(shell if $(CC) -mno-red-zone -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-mno-red-zone"; fi ) 
CFLAGS += -mcmodel=kernel
CFLAGS += -pipe
# generates worse code, but makes the assembly much more readable:
CFLAGS += -fno-reorder-blocks	
# work around early gcc 3.1 bugs. Later snapshots should this already fixed.
CFLAGS += -fno-strength-reduce
# make sure all inline functions are inlined
CFLAGS += -finline-limit=3000

#CFLAGS += -g

# prevent gcc from keeping the stack 16 byte aligned (FIXME)
#CFLAGS += -mpreferred-stack-boundary=2

HEAD := arch/x86_64/kernel/head.o arch/x86_64/kernel/head64.o arch/x86_64/kernel/init_task.o

SUBDIRS := arch/x86_64/tools $(SUBDIRS) arch/x86_64/kernel arch/x86_64/mm arch/x86_64/lib
CORE_FILES := arch/x86_64/kernel/kernel.o $(CORE_FILES)
CORE_FILES +=  arch/x86_64/mm/mm.o
LIBS := $(TOPDIR)/arch/x86_64/lib/lib.a $(LIBS)

CLEAN_FILES += include/asm-x86_64/offset.h

ifdef CONFIG_IA32_EMULATION
SUBDIRS += arch/x86_64/ia32
CORE_FILES += arch/x86_64/ia32/ia32.o
endif

ifdef CONFIG_HOSTFS
SUBDIRS += arch/x86_64/hostfs
core-$(CONFIG_HOSTFS) += arch/x86_64/hostfs/hostfs.o
endif

CORE_FILES += $(core-y)

arch/x86_64/tools: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/x86_64/tools 

arch/x86_64/kernel: dummy 
	$(MAKE) linuxsubdirs SUBDIRS=arch/x86_64/kernel

arch/x86_64/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/x86_64/mm

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

vmlinux: arch/x86_64/vmlinux.lds

checkoffset: FORCE
	make -C arch/$(ARCH)/tools $(TOPDIR)/include/asm-x86_64/offset.h

FORCE: ;

.PHONY: zImage bzImage compressed zlilo bzlilo zdisk bzdisk install \
		clean archclean archmrproper archdep checkoffset

bzImage: checkoffset vmlinux
	@$(MAKEBOOT) bzImage

bzImage-padded: checkoffset vmlinux
	@$(MAKEBOOT) bzImage-padded

tmp:
	@$(MAKEBOOT) BOOTIMAGE=bzImage zlilo
bzlilo: checkoffset vmlinux
	@$(MAKEBOOT) BOOTIMAGE=bzImage zlilo

zdisk: checkoffset vmlinux
	@$(MAKEBOOT) BOOTIMAGE=zImage zdisk

bzdisk: checkoffset vmlinux
	@$(MAKEBOOT) BOOTIMAGE=bzImage zdisk

install: checkoffset vmlinux
	@$(MAKEBOOT) BOOTIMAGE=bzImage install

archclean:
	@$(MAKEBOOT) clean
	$(MAKE) -C $(TOPDIR)/arch/x86_64/tools clean

archmrproper:
	rm -f $(TOPDIR)/arch/x86_64/tools/offset.h
	rm -f $(TOPDIR)/arch/x86_64/tools/offset.tmp
	rm -f $(TOPDIR)/include/asm-x86_64/offset.h

archdep:
	@$(MAKE) -C $(TOPDIR)/arch/x86_64/tools all
	@$(MAKEBOOT) dep
