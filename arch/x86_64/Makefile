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
#           ....and later removed it again....
#
# $Id: Makefile,v 1.31 2002/03/22 15:56:07 ak Exp $

#
# early bootup linking needs 32bit. You can either use real 32bit tools
# here or 64bit tools in 32bit mode.
#
IA32_CC := $(CROSS_COMPILE)gcc -m32 -O2 -fomit-frame-pointer -nostdinc -I $(HPATH)
IA32_LD := $(CROSS_COMPILE)ld -m elf_i386
IA32_AS := $(CROSS_COMPILE)gcc -m32 -Wa,--32 -D__ASSEMBLY__ -traditional -c -nostdinc -I $(HPATH) 
IA32_OBJCOPY := $(CROSS_COMPILE)objcopy
IA32_CPP := $(CROSS_COMPILE)gcc -m32 -E
export IA32_CC IA32_LD IA32_AS IA32_OBJCOPY IA32_CPP


LD=$(CROSS_COMPILE)ld -m elf_x86_64
OBJCOPYFLAGS	:= -O binary -R .note -R .comment -S
LDFLAGS_vmlinux := -T arch/x86_64/vmlinux.lds -e stext

CFLAGS += -mno-red-zone
CFLAGS += -mcmodel=kernel
CFLAGS += -pipe
# this makes reading assembly source easier
CFLAGS += -fno-reorder-blocks	
CFLAGS += -finline-limit=2000
#CFLAGS += -g

HEAD := arch/x86_64/kernel/head.o arch/x86_64/kernel/head64.o arch/x86_64/kernel/init_task.o

SUBDIRS += arch/x86_64/kernel arch/x86_64/mm arch/x86_64/lib
CORE_FILES := arch/x86_64/kernel/kernel.o $(CORE_FILES)
CORE_FILES +=  arch/x86_64/mm/mm.o
LIBS := $(TOPDIR)/arch/x86_64/lib/lib.a $(LIBS)

ifdef CONFIG_IA32_EMULATION
SUBDIRS += arch/x86_64/ia32
CORE_FILES += arch/x86_64/ia32/ia32.o
endif

ifdef CONFIG_PCI
SUBDIRS += arch/x86_64/pci
DRIVERS += arch/x86_64/pci/pci.o
endif

CORE_FILES += $(core-y)

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

vmlinux: arch/x86_64/vmlinux.lds

.PHONY: zImage bzImage compressed zlilo bzlilo zdisk bzdisk install \
		clean archclean archmrproper

bzImage: vmlinux
	@$(MAKEBOOT) bzImage

bzImage-padded: vmlinux
	@$(MAKEBOOT) bzImage-padded

tmp:
	@$(MAKEBOOT) BOOTIMAGE=bzImage zlilo

bzlilo: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=bzImage zlilo

bzdisk: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=bzImage zdisk

install: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=bzImage install

archclean:
	@$(MAKEBOOT) clean

archmrproper:


prepare: include/asm-$(ARCH)/offset.h

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/offset.h.tmp: arch/$(ARCH)/kernel/asm-offsets.s
	@$(generate-asm-offsets.h) < $< > $@

include/asm-$(ARCH)/offset.h: include/asm-$(ARCH)/offset.h.tmp
	@echo -n '  Generating $@'
	@$(update-if-changed)

CLEAN_FILES += include/asm-$(ARCH)/offset.h.tmp \
	       include/asm-$(ARCH)/offset.h