#
# arch/arm/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2001 by Russell King

LDFLAGS_vmlinux	:=-p -X
LDFLAGS_BLOB	:=--format binary
AFLAGS_vmlinux.lds.o = -DTEXTADDR=$(TEXTADDR) -DDATAADDR=$(DATAADDR)
OBJCOPYFLAGS	:=-O binary -R .note -R .comment -S
GZFLAGS		:=-9
#CFLAGS		+=-pipe

CFLAGS		:=$(CFLAGS:-O2=-Os)

ifeq ($(CONFIG_FRAME_POINTER),y)
CFLAGS		+=-fno-omit-frame-pointer -mapcs -mno-sched-prolog
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
# Note that GCC does not numerically define an architecture version
# macro, but instead defines a whole series of macros which makes
# testing for a specific architecture or later rather impossible.
#
# Note - GCC does accept -march=armv5te, but someone messed up the assembler or the
# gcc specs file - this needs fixing properly - ie in gcc and/or binutils.
arch-$(CONFIG_CPU_32v5)		:=-D__LINUX_ARM_ARCH__=5 -march=armv5t
arch-$(CONFIG_CPU_32v4)		:=-D__LINUX_ARM_ARCH__=4 -march=armv4
arch-$(CONFIG_CPU_32v3)		:=-D__LINUX_ARM_ARCH__=3 -march=armv3

# This selects how we optimise for the processor.
tune-$(CONFIG_CPU_ARM610)	:=-mtune=arm610
tune-$(CONFIG_CPU_ARM710)	:=-mtune=arm710
tune-$(CONFIG_CPU_ARM720T)	:=-mtune=arm7tdmi
tune-$(CONFIG_CPU_ARM920T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM922T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM926T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_SA110)	:=-mtune=strongarm110
tune-$(CONFIG_CPU_SA1100)	:=-mtune=strongarm1100
tune-$(CONFIG_CPU_XSCALE)	:=-mtune=strongarm -Wa,-mxscale #-mtune=xscale

# Force -mno-fpu to be passed to the assembler.  Some versions of gcc don't
# do this with -msoft-float
CFLAGS_BOOT	:=$(apcs-y) $(arch-y) $(tune-y) -mshort-load-bytes -msoft-float -Wa,-mno-fpu -Uarm
CFLAGS		+=$(apcs-y) $(arch-y) $(tune-y) -mshort-load-bytes -msoft-float -Wa,-mno-fpu -Uarm
AFLAGS		+=$(apcs-y) $(arch-y) $(tune-y) -mno-fpu -msoft-float -Wa,-mno-fpu

#Default value
DATAADDR	:= .

ifeq ($(CONFIG_CPU_26),y)
PROCESSOR	:= armo
head-y		:= arch/arm/mach-arc/head.o arch/arm/kernel/init_task.o
LDFLAGS_BLOB	+= --oformat elf26-littlearm
  ifeq ($(CONFIG_ROM_KERNEL),y)
    DATAADDR	 := 0x02080000
    textaddr-y	 := 0x03800000
  else
    textaddr-y	 := 0x02080000
  endif
endif

ifeq ($(CONFIG_CPU_32),y)
PROCESSOR	:= armv
head-y		:= arch/arm/kernel/head.o arch/arm/kernel/init_task.o
LDFLAGS_BLOB	+= --oformat elf32-littlearm
textaddr-y	:= 0xC0008000
endif

 machine-$(CONFIG_ARCH_ARCA5K)	   := arc
 machine-$(CONFIG_ARCH_RPC)	   := rpc
 machine-$(CONFIG_ARCH_EBSA110)	   := ebsa110
 machine-$(CONFIG_ARCH_CLPS7500)   := clps7500
  incdir-$(CONFIG_ARCH_CLPS7500)   := cl7500
 machine-$(CONFIG_FOOTBRIDGE)	   := footbridge
  incdir-$(CONFIG_FOOTBRIDGE)	   := ebsa285
textaddr-$(CONFIG_ARCH_CO285)	   := 0x60008000
 machine-$(CONFIG_ARCH_CO285)	   := footbridge
  incdir-$(CONFIG_ARCH_CO285)	   := ebsa285
 machine-$(CONFIG_ARCH_FTVPCI)	   := ftvpci
  incdir-$(CONFIG_ARCH_FTVPCI)	   := nexuspci
 machine-$(CONFIG_ARCH_TBOX)	   := tbox
 machine-$(CONFIG_ARCH_SHARK)	   := shark
 machine-$(CONFIG_ARCH_SA1100)	   := sa1100
ifeq ($(CONFIG_ARCH_SA1100),y)
# SA1111 DMA bug: we don't want the kernel to live in precious DMA-able memory
textaddr-$(CONFIG_SA1111)	   := 0xc0208000
endif
 machine-$(CONFIG_ARCH_PXA)	   := pxa
 machine-$(CONFIG_ARCH_L7200)	   := l7200
 machine-$(CONFIG_ARCH_INTEGRATOR) := integrator
 machine-$(CONFIG_ARCH_CAMELOT)	   := epxa10db
textaddr-$(CONFIG_ARCH_CLPS711X)   := 0xc0028000
 machine-$(CONFIG_ARCH_CLPS711X)   := clps711x
textaddr-$(CONFIG_ARCH_FORTUNET)   := 0xc0008000
 machine-$(CONFIG_ARCH_ANAKIN)	   := anakin
 machine-$(CONFIG_ARCH_IOP3XX)	   := iop3xx
 machine-$(CONFIG_ARCH_ADIFCC)	   := adifcc

MACHINE  := $(machine-y)
TEXTADDR := $(textaddr-y)
ifeq ($(incdir-y),)
incdir-y := $(MACHINE)
endif
INCDIR   := $(incdir-y)
  
export	MACHINE PROCESSOR TEXTADDR GZFLAGS CFLAGS_BOOT

# Do we have FASTFPE?
FASTFPE		:=arch/arm/fastfpe
ifeq ($(FASTFPE),$(wildcard $(FASTFPE)))
FASTFPE_OBJ	:=$(FASTFPE)/
endif

# If we have a machine-specific directory, then include it in the build.
core-y				+= arch/arm/kernel/ arch/arm/mm/ arch/arm/common/
ifneq ($(MACHINE),)
core-y				+= arch/arm/mach-$(MACHINE)/
endif
core-$(CONFIG_FPE_NWFPE)	+= arch/arm/nwfpe/
core-$(CONFIG_FPE_FASTFPE)	+= $(FASTFPE_OBJ)

drivers-$(CONFIG_ARCH_CLPS7500)	+= drivers/acorn/char/
drivers-$(CONFIG_ARCH_L7200)	+= drivers/acorn/char/

libs-y				+= arch/arm/lib/

# Default target when executing plain make
all: zImage

boot := arch/arm/boot

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

.PHONY: maketools FORCE
maketools: include/asm-arm/.arch include/asm-arm/.proc \
	   include/asm-arm/constants.h include/linux/version.h FORCE
	$(Q)$(MAKE) $(build)=arch/arm/tools include/asm-arm/mach-types.h

# Convert bzImage to zImage
bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/zImage

zImage Image bootpImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

zinstall install: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

MRPROPER_FILES	+= \
	include/asm-arm/arch include/asm-arm/.arch \
	include/asm-arm/proc include/asm-arm/.proc \
	include/asm-arm/constants.h* \
	include/asm-arm/mach-types.h

# We use MRPROPER_FILES and CLEAN_FILES now
archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

# My testing targets (that short circuit a few dependencies)
zImg:;	$(Q)$(MAKE) $(build)=$(boot) $(boot)/zImage
Img:;	$(Q)$(MAKE) $(build)=$(boot) $(boot)/Image
bp:;	$(Q)$(MAKE) $(build)=$(boot) $(boot)/bootpImage
i:;	$(Q)$(MAKE) $(build)=$(boot) install
zi:;	$(Q)$(MAKE) $(build)=$(boot) zinstall

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
				   include/asm-arm/.arch include/asm-arm/.proc \
				   include/config/MARKER

include/asm-$(ARCH)/constants.h: arch/$(ARCH)/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)

define archhelp
  echo  '* zImage        - Compressed kernel image (arch/$(ARCH)/boot/zImage)'
  echo  '  Image         - Uncompressed kernel image (arch/$(ARCH)/boot/Image)'
  echo  '  bootpImage    - Combined zImage and initial RAM disk' 
  echo  '  initrd        - Create an initial image'
  echo  '  install       - Install uncompressed kernel'
  echo  '  zinstall      - Install compressed kernel'
  echo  '                  Install using (your) ~/bin/installkernel or'
  echo  '                  (distribution) /sbin/installkernel or'
  echo  '                  install to $$(INSTALL_PATH) and run lilo'
endef
