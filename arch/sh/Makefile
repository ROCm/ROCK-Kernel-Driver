# $Id: Makefile,v 1.17 2003/05/20 03:12:54 lethal Exp $
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1999  Kaz Kojima
# Copyright (C) 2002, 2003  Paul Mundt
# Copyright (C) 2002  M. R. Brown
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#

#
# We don't necessarily agree with the top-level Makefile with regards to what
# does and does not qualify as a noconfig_targets rule. In this case, we're
# still dependant on .config settings in order for core-y (machdir-y in
# particular) to resolve the proper directory. So we just manually include it
# if it hasn't been already..
# 
ifndef include_config
-include .config
endif

cpu-y				:= -mb
cpu-$(CONFIG_CPU_LITTLE_ENDIAN)	:= -ml

cpu-$(CONFIG_CPU_SH2)		+= -m2
cpu-$(CONFIG_CPU_SH3)		+= -m3
cpu-$(CONFIG_CPU_SH4)		+= -m4 -mno-implicit-fp

ifdef CONFIG_SH_KGDB
CFLAGS :=$(CFLAGS:-fomit-frame-pointer=) -g
AFLAGS += -g
ifdef CONFIG_KGDB_MORE
CFLAGS += $(shell echo $(CONFIG_KGDB_OPTIONS) | sed -e 's/"//g')
endif
endif

OBJCOPYFLAGS	:= -O binary -R .note -R .comment -R .stab -R .stabstr -S

#
# Choosing incompatible machines durings configuration will result in
# error messages during linking.
#
LDFLAGS_vmlinux     += -e _stext

ifdef CONFIG_CPU_LITTLE_ENDIAN
LDFLAGS_vmlinux     +=  --defsym 'jiffies=jiffies_64' -EL
else
LDFLAGS_vmlinux     +=  --defsym 'jiffies=jiffies_64+4' -EB
endif

CFLAGS		+= -pipe $(cpu-y)
AFLAGS		+= $(cpu-y)

head-y := arch/sh/kernel/head.o arch/sh/kernel/init_task.o

LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

core-y				+= arch/sh/kernel/ arch/sh/mm/

# Boards
machdir-$(CONFIG_SH_SOLUTION_ENGINE)		:= se/770x
machdir-$(CONFIG_SH_7751_SOLUTION_ENGINE)	:= se/7751
machdir-$(CONFIG_SH_STB1_HARP)			:= harp
machdir-$(CONFIG_SH_STB1_OVERDRIVE)		:= overdrive
machdir-$(CONFIG_SH_HP620)			:= hp6xx/hp620
machdir-$(CONFIG_SH_HP680)			:= hp6xx/hp680
machdir-$(CONFIG_SH_HP690)			:= hp6xx/hp690
machdir-$(CONFIG_SH_CQREEK)			:= cqreek
machdir-$(CONFIG_SH_DMIDA)			:= dmida
machdir-$(CONFIG_SH_EC3104)			:= ec3104
machdir-$(CONFIG_SH_SATURN)			:= saturn
machdir-$(CONFIG_SH_DREAMCAST)			:= dreamcast
machdir-$(CONFIG_SH_CAT68701)			:= cat68701
machdir-$(CONFIG_SH_BIGSUR)			:= bigsur
machdir-$(CONFIG_SH_SH2000)			:= sh2000
machdir-$(CONFIG_SH_ADX)			:= adx
machdir-$(CONFIG_SH_MPC1211)			:= mpc1211
machdir-$(CONFIG_SH_UNKNOWN)			:= unknown

incdir-y			:= $(machdir-y)

incdir-$(CONFIG_SH_SOLUTION_ENGINE)		:= se
incdir-$(CONFIG_SH_7751_SOLUTION_ENGINE)	:= se7751
incdir-$(CONFIG_SH_HP600)			:= hp6xx

core-y				+= arch/sh/boards/$(machdir-y)/

# Companion chips
core-$(CONFIG_HD64461)	+= arch/sh/cchips/hd6446x/hd64461/
core-$(CONFIG_HD64465)	+= arch/sh/cchips/hd6446x/hd64465/

cpuincdir-$(CONFIG_CPU_SH2)	:= cpu-sh2
cpuincdir-$(CONFIG_CPU_SH3)	:= cpu-sh3
cpuincdir-$(CONFIG_CPU_SH4)	:= cpu-sh4

libs-y				+= arch/sh/lib/	$(LIBGCC)

boot := arch/sh/boot

AFLAGS_vmlinux.lds.o := -traditional

prepare: target_links

.PHONY: target_links FORCE
target_links:
	@echo '  Making asm-sh/cpu -> asm-sh/$(cpuincdir-y) link'
	@ln -sf $(cpuincdir-y) include/asm-sh/cpu

	@echo '  Making asm-sh/mach -> asm-sh/$(incdir-y) link'
	@ln -sf $(incdir-y) include/asm-sh/mach

	$(Q)$(MAKE) $(build)=arch/sh/tools include/asm-sh/machtypes.h

BOOTIMAGE=arch/sh/boot/zImage
zImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(BOOTIMAGE)

compressed: zImage

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)
