#
# ia64/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1998-2001 by David Mosberger-Tang <davidm@hpl.hp.com>
#

NM := $(CROSS_COMPILE)nm -B
AWK := awk

export AWK

OBJCOPYFLAGS := --strip-all
LDFLAGS_vmlinux := -static
AFLAGS_KERNEL := -mconstant-gp
EXTRA	=

CFLAGS := $(CFLAGS) -pipe $(EXTRA) -ffixed-r13 -mfixed-range=f10-f15,f32-f127 \
	  -falign-functions=32
# -ffunction-sections
CFLAGS_KERNEL := -mconstant-gp

GCC_VERSION=$(shell $(CC) -v 2>&1 | fgrep 'gcc version' | cut -f3 -d' ' | cut -f1 -d'.')

ifneq ($(GCC_VERSION),2)
	CFLAGS += -frename-registers --param max-inline-insns=5000
endif

ifeq ($(CONFIG_ITANIUM_BSTEP_SPECIFIC),y)
	CFLAGS += -mb-step
endif

HEAD := arch/$(ARCH)/kernel/head.o arch/$(ARCH)/kernel/init_task.o

libs-y				+= arch/$(ARCH)/lib/
core-y				+= arch/$(ARCH)/kernel/ arch/$(ARCH)/mm/
core-$(CONFIG_IA32_SUPPORT)	+= arch/$(ARCH)/ia32/
core-$(CONFIG_IA64_DIG) 	+= arch/$(ARCH)/dig/
core-$(CONFIG_IA64_GENERIC) 	+= arch/$(ARCH)/hp/ arch/$(ARCH)/dig/
core-$(CONFIG_IA64_HP_SIM)	+= arch/$(ARCH)/hp/
core-$(CONFIG_IA64_HP_ZX1)	+= arch/$(ARCH)/hp/ arch/$(ARCH)/dig/
core-$(CONFIG_IA64_SGI_SN)	+= arch/$(ARCH)/sn/kernel arch/$(ARCH)/sn/io \
				   arch/$(ARCH)/sn/fakeprom
drivers-$(CONFIG_PCI)		+= arch/$(ARCH)/pci/
drivers-$(CONFIG_IA64_HP_ZX1)	+= arch/$(ARCH)/hp/common/ arch/$(ARCH)/hp/zx1/

ifdef CONFIG_IA64_SGI_SN
	CFLAGS			+= -DBRINGUP
        SUBDIRS         	+= arch/$(ARCH)/sn/fakeprom
endif

core-$(CONFIG_IA32_SUPPORT)	+= arch/$(ARCH)/ia32/

libs-y				+= arch/$(ARCH)/lib/
core-y				+= arch/$(ARCH)/kernel/ arch/$(ARCH)/mm/
drivers-$(CONFIG_PCI)		+= arch/$(ARCH)/pci/
drivers-$(CONFIG_IA64_HP_ZX1)	+= arch/$(ARCH)/hp/common/ arch/$(ARCH)/hp/zx1/

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

compressed: vmlinux
	$(OBJCOPY) $(OBJCOPYFLAGS) vmlinux vmlinux-tmp
	gzip vmlinux-tmp
	mv vmlinux-tmp.gz vmlinux.gz

rawboot:
	@$(MAKEBOOT) rawboot

archclean:
	@$(MAKEBOOT) clean

archmrproper:
	@$(MAKE) -C arch/$(ARCH)/tools mrproper

prepare: $(TOPDIR)/include/asm-ia64/offsets.h

$(TOPDIR)/include/asm-ia64/offsets.h: include/asm include/linux/version.h \
				      include/config/MARKER
	@$(MAKE) -C arch/$(ARCH)/tools $@
