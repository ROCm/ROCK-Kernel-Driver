#
# ia64/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1998-2003 by David Mosberger-Tang <davidm@hpl.hp.com>
#

NM := $(CROSS_COMPILE)nm -B

export AWK

OBJCOPYFLAGS	:= --strip-all
LDFLAGS_vmlinux	:= -static
LDFLAGS_MODULE	+= -T arch/ia64/module.lds
AFLAGS_KERNEL	:= -mconstant-gp
EXTRA		:=

cflags-y	:= -pipe $(EXTRA) -ffixed-r13 -mfixed-range=f10-f15,f32-f127 \
		   -falign-functions=32
CFLAGS_KERNEL	:= -mconstant-gp

GCC_VERSION=$(shell $(CC) -v 2>&1 | fgrep 'gcc version' | cut -f3 -d' ' | cut -f1 -d'.')

GAS_STATUS=$(shell arch/ia64/scripts/check-gas $(CC) $(OBJDUMP))

ifeq ($(GAS_STATUS),buggy)
$(error Sorry, you need a newer version of the assember, one that is built from	\
	a source-tree that post-dates 18-Dec-2002.  You can find a pre-compiled	\
	static binary of such an assembler at:					\
										\
		ftp://ftp.hpl.hp.com/pub/linux-ia64/gas-030124.tar.gz)
endif

ifneq ($(GCC_VERSION),2)
	cflags-y += -frename-registers --param max-inline-insns=5000
endif

cflags-$(CONFIG_ITANIUM_BSTEP_SPECIFIC)	+= -mb-step
cflags-$(CONFIG_IA64_SGI_SN)		+= -DBRINGUP

CFLAGS += $(cflags-y)
head-y := arch/ia64/kernel/head.o arch/ia64/kernel/init_task.o

libs-y				+= arch/ia64/lib/
core-y				+= arch/ia64/kernel/ arch/ia64/mm/
core-$(CONFIG_IA32_SUPPORT)	+= arch/ia64/ia32/
core-$(CONFIG_IA64_DIG) 	+= arch/ia64/dig/
core-$(CONFIG_IA64_GENERIC) 	+= arch/ia64/dig/ 
core-$(CONFIG_IA64_HP_ZX1)	+= arch/ia64/dig/
core-$(CONFIG_IA64_SGI_SN)	+= arch/ia64/sn/

drivers-$(CONFIG_PCI)		+= arch/ia64/pci/
drivers-$(CONFIG_IA64_HP_SIM)	+= arch/ia64/hp/sim/
drivers-$(CONFIG_IA64_HP_ZX1)	+= arch/ia64/hp/common/ arch/ia64/hp/zx1/
drivers-$(CONFIG_IA64_GENERIC)	+= arch/ia64/hp/common/ arch/ia64/hp/zx1/ arch/ia64/hp/sim/

boot := arch/ia64/boot
tools := arch/ia64/tools

.PHONY: boot compressed include/asm-ia64/offsets.h

all: prepare vmlinux

compressed: vmlinux.gz

vmlinux.gz: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) vmlinux.gz

check: vmlinux
	arch/ia64/scripts/unwcheck.sh vmlinux

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)
	$(Q)$(MAKE) $(clean)=$(tools)

CLEAN_FILES += include/asm-ia64/offsets.h vmlinux.gz bootloader

prepare: include/asm-ia64/offsets.h

boot:	lib/lib.a vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

include/asm-ia64/offsets.h: include/asm include/linux/version.h include/config/MARKER
	$(Q)$(MAKE) $(build)=$(tools) $@

define archhelp
  echo '  compressed	- Build compressed kernel image'
  echo '  boot		- Build vmlinux and bootloader for Ski simulator'
endef
