#
# arch/arm26/Makefile
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

ifeq ($(CONFIG_FRAME_POINTER),y)
CFLAGS		+=-fno-omit-frame-pointer -mno-sched-prolog
endif

ifeq ($(CONFIG_DEBUG_INFO),y)
CFLAGS		+=-g
endif

# Force -mno-fpu to be passed to the assembler.  Some versions of gcc don't
# do this with -msoft-float
CFLAGS_BOOT	:=-mapcs-26 -mcpu=arm3 -mshort-load-bytes -msoft-float -Wa,-mno-fpu -Uarm
CFLAGS		+=-mapcs-26 -mcpu=arm3 -mshort-load-bytes -msoft-float -Wa,-mno-fpu -Uarm
AFLAGS		+=-mapcs-26 -mcpu=arm3 -mno-fpu -msoft-float -Wa,-mno-fpu

head-y		:= arch/arm26/machine/head.o arch/arm26/kernel/init_task.o
LDFLAGS_BLOB	+= --oformat elf32-littlearm

ifeq ($(CONFIG_XIP_KERNEL),y)
  TEXTADDR	 := 0x03880000
  DATAADDR	 := 0x02080000
else
  TEXTADDR	 := 0x02080000
  DATAADDR       := .
endif

ifeq ($(incdir-y),)
incdir-y :=
endif
INCDIR   :=
  
export	MACHINE TEXTADDR GZFLAGS CFLAGS_BOOT

# If we have a machine-specific directory, then include it in the build.
core-y				+= arch/arm26/kernel/ arch/arm26/mm/ arch/arm26/machine/
core-$(CONFIG_FPE_NWFPE)	+= arch/arm26/nwfpe/

libs-y				+= arch/arm26/lib/

# Default target when executing plain make
all: zImage

boot := arch/arm26/boot

prepare: include/asm-$(ARCH)/asm_offsets.h
CLEAN_FILES += include/asm-$(ARCH)/asm_offsets.h


.PHONY: maketools FORCE
maketools: FORCE
	

# Convert bzImage to zImage
bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/zImage

zImage Image bootpImage xipImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

zinstall install: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

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
	if [ -f arch/arm26/def-configs/$$CFG ]; then \
	  [ -f .config ] && mv -f .config .config.old; \
	  cp arch/arm26/def-configs/$$CFG .config; \
	  echo "*** Default configuration for $$CFG installed"; \
	  echo "*** Next, you may run 'make oldconfig'"; \
	else \
	  echo "$$CFG does not exist"; \
	fi; \
	)

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/asm_offsets.h: arch/$(ARCH)/kernel/asm-offsets.s
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
