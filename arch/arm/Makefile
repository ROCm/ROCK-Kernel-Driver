#
# arch/arm/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2001 by Russell King

LDFLAGS_vmlinux	:=-p --no-undefined -X
LDFLAGS_BLOB	:=--format binary
AFLAGS_vmlinux.lds.o = -DTEXTADDR=$(TEXTADDR) -DDATAADDR=$(DATAADDR)
OBJCOPYFLAGS	:=-O binary -R .note -R .comment -S
GZFLAGS		:=-9
#CFLAGS		+=-pipe

ifeq ($(CONFIG_FRAME_POINTER),y)
CFLAGS		+=-fno-omit-frame-pointer -mapcs -mno-sched-prolog
endif

ifeq ($(CONFIG_CPU_BIG_ENDIAN),y)
CFLAGS		+= -mbig-endian
AS		+= -EB
LD		+= -EB
AFLAGS		+= -mbig-endian
else
CFLAGS		+= -mlittle-endian
AS		+= -EL
LD		+= -EL
AFLAGS		+= -mlittle-endian
endif

comma = ,

# This selects which instruction set is used.
# Note that GCC does not numerically define an architecture version
# macro, but instead defines a whole series of macros which makes
# testing for a specific architecture or later rather impossible.
arch-$(CONFIG_CPU_32v6)		:=-D__LINUX_ARM_ARCH__=6 $(call check_gcc,-march=armv6,-march=armv5t -Wa$(comma)-march=armv6)
arch-$(CONFIG_CPU_32v5)		:=-D__LINUX_ARM_ARCH__=5 $(call check_gcc,-march=armv5te,-march=armv4)
arch-$(CONFIG_CPU_32v4)		:=-D__LINUX_ARM_ARCH__=4 -march=armv4
arch-$(CONFIG_CPU_32v3)		:=-D__LINUX_ARM_ARCH__=3 -march=armv3

# This selects how we optimise for the processor.
tune-$(CONFIG_CPU_ARM610)	:=-mtune=arm610
tune-$(CONFIG_CPU_ARM710)	:=-mtune=arm710
tune-$(CONFIG_CPU_ARM720T)	:=-mtune=arm7tdmi
tune-$(CONFIG_CPU_ARM920T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM922T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM925T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_ARM926T)	:=-mtune=arm9tdmi
tune-$(CONFIG_CPU_SA110)	:=-mtune=strongarm110
tune-$(CONFIG_CPU_SA1100)	:=-mtune=strongarm1100
tune-$(CONFIG_CPU_XSCALE)	:=$(call check_gcc,-mtune=xscale,-mtune=strongarm110) -Wa,-mcpu=xscale
tune-$(CONFIG_CPU_V6)		:=-mtune=strongarm

# Need -Uarm for gcc < 3.x
CFLAGS		+=-mapcs-32 $(arch-y) $(tune-y) -mshort-load-bytes -msoft-float -Wa,-mno-fpu -Uarm
AFLAGS		+=-mapcs-32 $(arch-y) $(tune-y) -msoft-float -Wa,-mno-fpu

#Default value
DATAADDR	:= .

head-y		:= arch/arm/kernel/head.o arch/arm/kernel/init_task.o
textaddr-y	:= 0xC0008000

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
 machine-$(CONFIG_ARCH_IOP3XX)	   := iop3xx
 machine-$(CONFIG_ARCH_IXP4XX)	   := ixp4xx
 machine-$(CONFIG_ARCH_OMAP)	   := omap
 machine-$(CONFIG_ARCH_S3C2410)	   := s3c2410
 machine-$(CONFIG_ARCH_LH7A40X)	   := lh7a40x
 machine-$(CONFIG_ARCH_VERSATILE_PB) := versatile

TEXTADDR := $(textaddr-y)
ifeq ($(incdir-y),)
incdir-y := $(machine-y)
endif
INCDIR   := arch-$(incdir-y)
  
export	TEXTADDR GZFLAGS

# Do we have FASTFPE?
FASTFPE		:=arch/arm/fastfpe
ifeq ($(FASTFPE),$(wildcard $(FASTFPE)))
FASTFPE_OBJ	:=$(FASTFPE)/
endif

# If we have a machine-specific directory, then include it in the build.
core-y				+= arch/arm/kernel/ arch/arm/mm/ arch/arm/common/
ifneq ($(machine-y),)
core-y				+= arch/arm/mach-$(machine-y)/
endif
core-$(CONFIG_FPE_NWFPE)	+= arch/arm/nwfpe/
core-$(CONFIG_FPE_FASTFPE)	+= $(FASTFPE_OBJ)

drivers-$(CONFIG_OPROFILE)      += arch/arm/oprofile/
drivers-$(CONFIG_ARCH_CLPS7500)	+= drivers/acorn/char/
drivers-$(CONFIG_ARCH_L7200)	+= drivers/acorn/char/

libs-y				+= arch/arm/lib/

# Default target when executing plain make
all: zImage

boot := arch/arm/boot

#	Update machine arch and proc symlinks if something which affects
#	them changed.  We use .arch to indicate when they were updated
#	last, otherwise make uses the target directory mtime.

include/asm-arm/.arch: $(wildcard include/config/arch/*.h) include/config/MARKER
	@echo '  SYMLINK include/asm-arm/arch -> include/asm-arm/$(INCDIR)'
ifneq ($(KBUILD_SRC),)
	$(Q)mkdir -p include/asm-arm
	$(Q)ln -fsn $(srctree)/include/asm-arm/$(INCDIR) include/asm-arm/arch
else
	$(Q)ln -fsn $(INCDIR) include/asm-arm/arch
endif
	@touch $@

prepare: maketools include/asm-arm/.arch

.PHONY: maketools FORCE
maketools: include/asm-arm/constants.h include/linux/version.h FORCE
	$(Q)$(MAKE) $(build)=arch/arm/tools include/asm-arm/mach-types.h

# Convert bzImage to zImage
bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/zImage

zImage Image bootpImage uImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

zinstall install: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

CLEAN_FILES += include/asm-arm/constants.h* include/asm-arm/mach-types.h \
	       include/asm-arm/arch include/asm-arm/.arch

# We use MRPROPER_FILES and CLEAN_FILES now
archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

# My testing targets (that short circuit a few dependencies)
zImg:;	$(Q)$(MAKE) $(build)=$(boot) $(boot)/zImage
Img:;	$(Q)$(MAKE) $(build)=$(boot) $(boot)/Image
bp:;	$(Q)$(MAKE) $(build)=$(boot) $(boot)/bootpImage
i:;	$(Q)$(MAKE) $(build)=$(boot) install
zi:;	$(Q)$(MAKE) $(build)=$(boot) zinstall

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/asm-arm/.arch

include/asm-$(ARCH)/constants.h: arch/$(ARCH)/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)

define archhelp
  echo  '* zImage        - Compressed kernel image (arch/$(ARCH)/boot/zImage)'
  echo  '  Image         - Uncompressed kernel image (arch/$(ARCH)/boot/Image)'
  echo  '  bootpImage    - Combined zImage and initial RAM disk' 
  echo  '  install       - Install uncompressed kernel'
  echo  '  zinstall      - Install compressed kernel'
  echo  '                  Install using (your) ~/bin/installkernel or'
  echo  '                  (distribution) /sbin/installkernel or'
  echo  '                  install to $$(INSTALL_PATH) and run lilo'
endef
