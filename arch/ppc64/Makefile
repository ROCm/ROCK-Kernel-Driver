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
# Changes for PPC by Gary Thomas
# Rewritten by Cort Dougan and Paul Mackerras
# Adjusted for PPC64 by Tom Gall
#

KERNELLOAD	:= 0xc000000000000000

HAS_BIARCH      := $(shell if $(CC) -m64 -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo y; else echo n; fi;)
ifeq ($(HAS_BIARCH),y)
AS              := $(AS) -a64
LD              := $(LD) -m elf64ppc
CC		:= $(CC) -m64
endif

CHECK		:= $(CHECK) -m64 -D__powerpc__=1

LDFLAGS		:= -m elf64ppc
LDFLAGS_vmlinux	:= -Bstatic -e $(KERNELLOAD) -Ttext $(KERNELLOAD)
CFLAGS		+= -msoft-float -pipe -Wno-uninitialized -mminimal-toc \
		   -mtraceback=none

ifeq ($(CONFIG_POWER4_ONLY),y)
	CFLAGS += $(call check_gcc,-mcpu=power4,)
else
	CFLAGS += $(call check_gcc,-mtune=power4,)
endif

# Enable unit-at-a-time mode when possible. It shrinks the
# kernel considerably.
CFLAGS += $(call check_gcc,-funit-at-a-time,)

head-y := arch/ppc64/kernel/head.o

libs-y				+= arch/ppc64/lib/
core-y				+= arch/ppc64/kernel/
core-y				+= arch/ppc64/mm/
core-$(CONFIG_XMON)		+= arch/ppc64/xmon/
drivers-$(CONFIG_OPROFILE)	+= arch/ppc64/oprofile/

boot := arch/ppc64/boot

boottarget-$(CONFIG_PPC_PSERIES) := zImage zImage.initrd
boottarget-$(CONFIG_PPC_ISERIES) := vmlinux.sminitrd vmlinux.initrd vmlinux.sm
$(boottarget-y): vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

bootimage-$(CONFIG_PPC_PSERIES) := zImage
bootimage-$(CONFIG_PPC_ISERIES) := vmlinux
BOOTIMAGE := $(bootimage-y)
install: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(BOOTIMAGE) $@

all: $(BOOTIMAGE)

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

prepare: include/asm-ppc64/offsets.h

arch/ppc64/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-ppc64/offsets.h: arch/ppc64/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)

define archhelp
  echo  '* zImage       - Compressed kernel image (arch/$(ARCH)/boot/zImage)'
  echo  '  zImage.initrd- Compressed kernel image with initrd attached,'
  echo  '		   sourced from arch/$(ARCH)/boot/ramdisk.image.gz'
  echo  '		   (arch/$(ARCH)/boot/zImage.initrd)'
endef

CLEAN_FILES += include/asm-ppc64/offsets.h
