#
# arch/arm/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2001 by Russell King

LDFLAGS_vmlinux	:=-p -X
OBJCOPYFLAGS	:=-O binary -R .note -R .comment -S
GZFLAGS		:=-9
#CFLAGS		+=-pipe

CFLAGS		:=$(CFLAGS:-O2=-Os)

ifneq ($(CONFIG_NO_FRAME_POINTER),y)
CFLAGS		:=$(CFLAGS: -fomit-frame-pointer=)
endif

ifeq ($(CONFIG_DEBUG_INFO),y)
CFLAGS		+=-g
endif

# Select CPU dependent flags.  Note that order of declaration is important;
# the options further down the list override previous items.
#
apcs-$(CONFIG_CPU_32)		:=-mapcs-32
apcs-$(CONFIG_CPU_26)		:=-mapcs-26 -mcpu=arm3

# This selects which instruction set is used.
# Note that GCC is lame - it doesn't numerically define an
# architecture version macro, but instead defines a whole
# series of macros.
arch-y				:=
arch-$(CONFIG_CPU_32v3)		:=-D__LINUX_ARM_ARCH__=3 -march=armv3
arch-$(CONFIG_CPU_32v4)		:=-D__LINUX_ARM_ARCH__=4 -march=armv4
arch-$(CONFIG_CPU_32v5)		:=-D__LINUX_ARM_ARCH__=5 -march=armv5
arch-$(CONFIG_CPU_XSCALE)	:=-D__LINUX_ARM_ARCH__=5 -march=armv4 -Wa,-mxscale #-march=armv5te

# This selects how we optimise for the processor.
tune-y				:=
tune-$(CONFIG_CPU_ARM610)	:=-mtune=arm610
tune-$(CONFIG_CPU_ARM710)	:=-mtune=arm710
tune-$(CONFIG_CPU_ARM720T)	:=-mtune=arm7tdmi
tune-$(CONFIG_CPU_ARM920T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM922T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM926T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_SA110)	:=-mtune=strongarm110
tune-$(CONFIG_CPU_SA1100)	:=-mtune=strongarm1100
tune-$(CONFIG_CPU_XSCALE)	:=-mtune=strongarm #-mtune=xscale

CFLAGS_BOOT	:=$(apcs-y) $(arch-y) $(tune-y) -mshort-load-bytes -msoft-float
CFLAGS		+=$(apcs-y) $(arch-y) $(tune-y) -mshort-load-bytes -msoft-float
AFLAGS		+=$(apcs-y) $(arch-y) -mno-fpu -msoft-float

ifeq ($(CONFIG_CPU_26),y)
PROCESSOR	:= armo
HEAD		:= arch/arm/mach-arc/head.o arch/arm/kernel/init_task.o
endif

ifeq ($(CONFIG_CPU_32),y)
PROCESSOR	 = armv
HEAD		:= arch/arm/kernel/head.o arch/arm/kernel/init_task.o
endif

ifeq ($(CONFIG_ARCH_ARCA5K),y)
MACHINE		 = arc
endif

ifeq ($(CONFIG_ARCH_RPC),y)
MACHINE		 = rpc
endif

ifeq ($(CONFIG_ARCH_EBSA110),y)
MACHINE		 = ebsa110
endif

ifeq ($(CONFIG_ARCH_CLPS7500),y)
MACHINE		 = clps7500
INCDIR		 = cl7500
endif

ifeq ($(CONFIG_FOOTBRIDGE),y)
MACHINE		 = footbridge
INCDIR		 = ebsa285
endif

ifeq ($(CONFIG_ARCH_CO285),y)
TEXTADDR	 = 0x60008000
MACHINE		 = footbridge
INCDIR		 = ebsa285
endif

ifeq ($(CONFIG_ARCH_FTVPCI),y)
MACHINE		 = ftvpci
INCDIR		 = nexuspci
endif

ifeq ($(CONFIG_ARCH_TBOX),y)
MACHINE		 = tbox
endif

ifeq ($(CONFIG_ARCH_SHARK),y)
MACHINE		 = shark
endif

ifeq ($(CONFIG_ARCH_SA1100),y)
ifeq ($(CONFIG_SA1111),y)
# SA1111 DMA bug: we don't want the kernel to live in precious DMA-able memory
TEXTADDR	 = 0xc0208000
endif
MACHINE		 = sa1100
endif

ifeq ($(CONFIG_ARCH_PXA),y)
MACHINE		 = pxa
endif

ifeq ($(CONFIG_ARCH_L7200),y)
MACHINE		 = l7200
endif

ifeq ($(CONFIG_ARCH_INTEGRATOR),y)
MACHINE		 = integrator
endif

ifeq ($(CONFIG_ARCH_CAMELOT),y)
MACHINE          = epxa10db
endif

ifeq ($(CONFIG_ARCH_CLPS711X),y)
TEXTADDR	 = 0xc0028000
MACHINE		 = clps711x
endif

ifeq ($(CONFIG_ARCH_FORTUNET),y)
TEXTADDR	 = 0xc0008000
endif

ifeq ($(CONFIG_ARCH_ANAKIN),y)
MACHINE		 = anakin
endif

ifeq ($(CONFIG_ARCH_IOP310),y)
MACHINE		 = iop310
endif

ifeq ($(CONFIG_ARCH_ADIFCC),y)
MACHINE		= adifcc
endif

export	MACHINE PROCESSOR TEXTADDR GZFLAGS CFLAGS_BOOT

# Only set INCDIR if its not already defined above
# Grr, ?= doesn't work as all the other assignment operators do.  Make bug?
ifeq ($(origin INCDIR), undefined)
INCDIR		:=$(MACHINE)
endif

ifeq ($(origin DATAADDR), undefined)
DATAADDR	:= .
endif

# Do we have FASTFPE?
FASTFPE		:=arch/arm/fastfpe
ifeq ($(FASTFPE),$(wildcard $(FASTFPE)))
FASTFPE_OBJ	:=$(FASTFPE)/
endif

# If we have a machine-specific directory, then include it in the build.
core-y				+= arch/arm/mach-$(MACHINE)/ \
				   arch/arm/kernel/ arch/arm/mm/
core-$(CONFIG_FPE_NWFPE)	+= arch/arm/nwfpe/
core-$(CONFIG_FPE_FASTFPE)	+= $(FASTFPE_OBJ)

drivers-$(CONFIG_ARCH_CLPS7500)	+= drivers/acorn/char/
drivers-$(CONFIG_ARCH_L7200))	+= drivers/acorn/char/

libs-y				+= arch/arm/lib/

MAKEBOOT	 =$(MAKE) -C arch/$(ARCH)/boot
MAKETOOLS	 =$(MAKE) -C arch/$(ARCH)/tools

#	Update machine arch and proc symlinks if something which affects
#	them changed.  We use .arch and .proc to indicate when they were
#	updated last, otherwise make uses the target directory mtime.

include/asm-arm/.arch: $(wildcard include/config/arch/*.h)
	@echo '  Making asm-arm/arch -> asm-arm/arch-$(INCDIR) symlink'
	@rm -f include/asm-arm/arch
	@ln -sf arch-$(INCDIR) include/asm-arm/arch
	@touch $@

include/asm-arm/.proc: $(wildcard include/config/cpu/32.h) $(wildcard include/config/cpu/26.h)
	@echo '  Making asm-arm/proc -> asm-arm/proc-$(PROCESSOR) symlink'
	@rm -f include/asm-arm/proc
	@ln -sf proc-$(PROCESSOR) include/asm-arm/proc
	@touch $@

prepare: maketools

.PHONY: maketools
maketools: include/asm-arm/.arch include/asm-arm/.proc \
	 include/asm-arm/constants.h include/linux/version.h FORCE
	@$(MAKETOOLS)

bzImage zImage zinstall Image bootpImage install: vmlinux
	@$(MAKEBOOT) $@

MRPROPER_FILES	+= \
	include/asm-arm/arch include/asm-arm/.arch \
	include/asm-arm/proc include/asm-arm/.proc \
	include/asm-arm/constants.h* \
	include/asm-arm/mach-types.h

# We use MRPROPER_FILES and CLEAN_FILES now
archmrproper: FORCE
	@/bin/true

archclean: FORCE
	@$(MAKEBOOT) clean

# My testing targets (that short circuit a few dependencies)
zImg:;	@$(MAKEBOOT) zImage
Img:;	@$(MAKEBOOT) Image
i:;	@$(MAKEBOOT) install
zi:;	@$(MAKEBOOT) zinstall
bp:;	@$(MAKEBOOT) bootpImage

#
# Configuration targets.  Use these to select a
# configuration for your architecture
%_config:
	@( \
	CFG=$(@:_config=); \
	if [ -f arch/arm/def-configs/$$CFG ]; then \
	  [ -f .config ] && mv -f .config .config.old; \
	  cp arch/arm/def-configs/$$CFG .config; \
	  echo "*** Default configuration for $$CFG installed"; \
	  echo "*** Next, you may run 'make oldconfig'"; \
	else \
	  echo "$$CFG does not exist"; \
	fi; \
	)

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/constants.h.tmp: arch/$(ARCH)/kernel/asm-offsets.s
	@$(generate-asm-offsets.h) < $< > $@

include/asm-$(ARCH)/constants.h: include/asm-$(ARCH)/constants.h.tmp
	@echo -n '  Generating $@'
	@$(update-if-changed)
