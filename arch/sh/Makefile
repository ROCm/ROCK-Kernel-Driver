# $Id: Makefile,v 1.35 2004/04/15 03:39:20 sugioka Exp $
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1999  Kaz Kojima
# Copyright (C) 2002, 2003, 2004  Paul Mundt
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

cflags-y				:= -mb
cflags-$(CONFIG_CPU_LITTLE_ENDIAN)	:= -ml

cflags-$(CONFIG_CPU_SH2)		+= -m2
cflags-$(CONFIG_CPU_SH3)		+= -m3
cflags-$(CONFIG_CPU_SH4)		+= -m4 \
	$(call check_gcc,-mno-implicit-fp,-m4-nofpu)

cflags-$(CONFIG_SH_DSP)			+= -Wa,-dsp
cflags-$(CONFIG_SH_KGDB)		+= -g

cflags-$(CONFIG_MORE_COMPILE_OPTIONS)	+= \
	$(shell echo $(CONFIG_COMPILE_OPTIONS) | sed -e 's/"//g')

OBJCOPYFLAGS	:= -O binary -R .note -R .comment -R .stab -R .stabstr -S

#
# Choosing incompatible machines durings configuration will result in
# error messages during linking.
#
LDFLAGS_vmlinux     += -e _stext

ifdef CONFIG_CPU_LITTLE_ENDIAN
LDFLAGS_vmlinux		+= --defsym 'jiffies=jiffies_64'
LDFLAGS			+= -EL
else
LDFLAGS_vmlinux		+= --defsym 'jiffies=jiffies_64+4'
LDFLAGS			+= -EB
endif

CFLAGS		+= -pipe $(cflags-y)
AFLAGS		+= $(cflags-y)

head-y := arch/sh/kernel/head.o arch/sh/kernel/init_task.o

LIBGCC := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

core-y				+= arch/sh/kernel/ arch/sh/mm/

#
# ramdisk/initrd support
# You need a compressed ramdisk image, named
# CONFIG_EMBEDDED_RAMDISK_IMAGE. Relative pathnames
# are relative to arch/sh/ramdisk/.
#
core-$(CONFIG_EMBEDDED_RAMDISK)	+= arch/sh/ramdisk/

# Boards
machdir-$(CONFIG_SH_SOLUTION_ENGINE)		:= se/770x
machdir-$(CONFIG_SH_7751_SOLUTION_ENGINE)	:= se/7751
machdir-$(CONFIG_SH_7300_SOLUTION_ENGINE)	:= se/7300
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
machdir-$(CONFIG_SH_SECUREEDGE5410)		:= snapgear
machdir-$(CONFIG_SH_HS7751RVOIP)		:= renesas/hs7751rvoip
machdir-$(CONFIG_SH_RTS7751R2D)			:= renesas/rts7751r2d
machdir-$(CONFIG_SH_7751_SYSTEMH)		:= renesas/systemh
machdir-$(CONFIG_SH_UNKNOWN)			:= unknown

incdir-y			:= $(notdir $(machdir-y))

incdir-$(CONFIG_SH_SOLUTION_ENGINE)		:= se
incdir-$(CONFIG_SH_7751_SOLUTION_ENGINE)	:= se7751
incdir-$(CONFIG_SH_7300_SOLUTION_ENGINE)        := se7300
incdir-$(CONFIG_SH_HP600)			:= hp6xx

core-y				+= arch/sh/boards/$(machdir-y)/

# Companion chips
core-$(CONFIG_HD64461)		+= arch/sh/cchips/hd6446x/hd64461/
core-$(CONFIG_HD64465)		+= arch/sh/cchips/hd6446x/hd64465/
core-$(CONFIG_VOYAGERGX)	+= arch/sh/cchips/voyagergx/

cpuincdir-$(CONFIG_CPU_SH2)	:= cpu-sh2
cpuincdir-$(CONFIG_CPU_SH3)	:= cpu-sh3
cpuincdir-$(CONFIG_CPU_SH4)	:= cpu-sh4

libs-y				:= arch/sh/lib/	$(libs-y) $(LIBGCC)

drivers-y			+= arch/sh/drivers/
drivers-$(CONFIG_OPROFILE)	+= arch/sh/oprofile/

boot := arch/sh/boot

AFLAGS_vmlinux.lds.o := -traditional

prepare: target_links

.PHONY: target_links FORCE

all: zImage

target_links:
	@echo '  Making asm-sh/cpu -> asm-sh/$(cpuincdir-y) link'
	@rm -f include/asm-sh/cpu
	@ln -sf $(cpuincdir-y) include/asm-sh/cpu

	@echo '  Making asm-sh/mach -> asm-sh/$(incdir-y) link'
	@rm -f include/asm-sh/mach
	@ln -sf $(incdir-y) include/asm-sh/mach

	$(Q)$(MAKE) $(build)=arch/sh/tools include/asm-sh/machtypes.h

zImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

compressed: zImage

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
	@echo '  zImage 	           - Compressed kernel image (arch/sh/boot/zImage)'
endef

