#
# ia64/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1998-2002 by David Mosberger-Tang <davidm@hpl.hp.com>
#

NM := $(CROSS_COMPILE)nm -B

export AWK

OBJCOPYFLAGS	:= --strip-all
LDFLAGS_vmlinux	:= -static
AFLAGS_KERNEL	:= -mconstant-gp
EXTRA		:=

cflags-y	:= -pipe $(EXTRA) -ffixed-r13 -mfixed-range=f10-f15,f32-f127 \
		   -falign-functions=32
CFLAGS_KERNEL	:= -mconstant-gp

GCC_VERSION=$(shell $(CC) -v 2>&1 | fgrep 'gcc version' | cut -f3 -d' ' | cut -f1 -d'.')

ifneq ($(GCC_VERSION),2)
	CFLAGS += -frename-registers --param max-inline-insns=5000
endif

cflags-$(CONFIG_ITANIUM_BSTEP_SPECIFIC)	+= -mb-step
cflags-$(CONFIG_IA64_SGI_SN)		+= -DBRINGUP

HEAD := arch/ia64/kernel/head.o arch/ia64/kernel/init_task.o

libs-y				+= arch/ia64/lib/
core-y				+= arch/ia64/kernel/ arch/ia64/mm/
core-$(CONFIG_IA32_SUPPORT)	+= arch/ia64/ia32/
core-$(CONFIG_IA64_DIG) 	+= arch/ia64/dig/
core-$(CONFIG_IA64_GENERIC) 	+= arch/ia64/dig/ arch/ia64/hp/common/ arch/ia64/hp/zx1/ \
				   arch/ia64/hp/sim/
core-$(CONFIG_IA64_HP_ZX1)	+= arch/ia64/dig/
core-$(CONFIG_IA64_SGI_SN)	+= arch/ia64/sn/kernel/ \
				   arch/ia64/sn/io/ \
				   arch/ia64/sn/io/sn2/ \
				   arch/ia64/sn/io/sn2/pcibr/ \
				   arch/ia64/sn/kernel/sn2/
drivers-$(CONFIG_PCI)		+= arch/ia64/pci/
drivers-$(CONFIG_IA64_HP_SIM)	+= arch/ia64/hp/sim/
drivers-$(CONFIG_IA64_HP_ZX1)	+= arch/ia64/hp/common/ arch/ia64/hp/zx1/
drivers-$(CONFIG_IA64_SGI_SN)	+= arch/ia64/sn/fakeprom/

makeboot =$(Q)$(MAKE) -f scripts/Makefile.build obj=arch/ia64/boot $(1)
maketool =$(Q)$(MAKE) -f scripts/Makefile.build obj=arch/ia64/tools $(1)

.PHONY: compressed archclean archmrproper include/asm-ia64/offsets.h

all compressed: vmlinux.gz

vmlinux.gz: vmlinux
	$(call makeboot,vmlinux.gz)

archmrproper:
archclean:
	$(Q)$(MAKE) -f scripts/Makefile.clean obj=arch/ia64/boot

CLEAN_FILES += include/asm-ia64/offsets.h vmlinux.gz bootloader

prepare: include/asm-ia64/offsets.h

include/asm-ia64/offsets.h: include/asm include/linux/version.h \
			    include/config/MARKER
	$(call maketool,$@)
