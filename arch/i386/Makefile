#
# i386/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" cleaning up for this architecture.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#
# 19990713  Artur Skawina <skawina@geocities.com>
#           Added '-march' and '-mpreferred-stack-boundary' support
#

LDFLAGS		:= -m elf_i386
OBJCOPYFLAGS	:= -O binary -R .note -R .comment -S
LDFLAGS_vmlinux :=
LDFLAGS_BLOB	:= --format binary --oformat elf32-i386

CFLAGS += -pipe

check_gcc = $(shell if $(CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi)

# prevent gcc from keeping the stack 16 byte aligned
CFLAGS += $(call check_gcc,-mpreferred-stack-boundary=2,)

align := $(subst -functions=0,,$(call check_gcc,-falign-functions=0,-malign-functions=0))

cflags-$(CONFIG_M386)		+= -march=i386
cflags-$(CONFIG_M486)		+= -march=i486
cflags-$(CONFIG_M586)		+= -march=i586
cflags-$(CONFIG_M586TSC)	+= -march=i586
cflags-$(CONFIG_M586MMX)	+= $(call check_gcc,-march=pentium-mmx,-march=i586)
cflags-$(CONFIG_M686)		+= -march=i686
cflags-$(CONFIG_MPENTIUMII)	+= $(call check_gcc,-march=pentium2,-march=i686)
cflags-$(CONFIG_MPENTIUMIII)	+= $(call check_gcc,-march=pentium3,-march=i686)
cflags-$(CONFIG_MPENTIUM4)	+= $(call check_gcc,-march=pentium4,-march=i686)
cflags-$(CONFIG_MK6)		+= $(call check_gcc,-march=k6,-march=i586)
cflags-$(CONFIG_MK7)		+= $(call check_gcc,-march=athlon,-march=i686 $(align)-functions=4)
cflags-$(CONFIG_MK8)		+= $(call check_gcc,-march=k8,$(call check_gcc,-march=athlon,-march=i686 $(align)-functions=4))
cflags-$(CONFIG_MCRUSOE)	+= -march=i686 $(align)-functions=0 $(align)-jumps=0 $(align)-loops=0
cflags-$(CONFIG_MWINCHIPC6)	+= $(call check_gcc,-march=winchip-c6,-march=i586)
cflags-$(CONFIG_MWINCHIP2)	+= $(call check_gcc,-march=winchip2,-march=i586)
cflags-$(CONFIG_MWINCHIP3D)	+= -march=i586
cflags-$(CONFIG_MCYRIXIII)	+= $(call check_gcc,-march=c3,-march=i486) $(align)-functions=0 $(align)-jumps=0 $(align)-loops=0
cflags-$(CONFIG_MVIAC3_2)	+= $(call check_gcc,-march=c3-2,-march=i686)

CFLAGS += $(cflags-y)

# Default subarch .c files
mcore-y  := mach-default

# Voyager subarch support
mflags-$(CONFIG_X86_VOYAGER)	:= -Iinclude/asm-i386/mach-voyager
mcore-$(CONFIG_X86_VOYAGER)	:= mach-voyager

# VISWS subarch support
mflags-$(CONFIG_X86_VISWS)	:= -Iinclude/asm-i386/mach-visws
mcore-$(CONFIG_X86_VISWS)	:= mach-visws

# NUMAQ subarch support
mflags-$(CONFIG_X86_NUMAQ)	:= -Iinclude/asm-i386/mach-numaq
mcore-$(CONFIG_X86_NUMAQ)	:= mach-default

# BIGSMP subarch support
mflags-$(CONFIG_X86_BIGSMP)	:= -Iinclude/asm-i386/mach-bigsmp
mcore-$(CONFIG_X86_BIGSMP)	:= mach-default

#Summit subarch support
mflags-$(CONFIG_X86_SUMMIT) := -Iinclude/asm-i386/mach-summit
mcore-$(CONFIG_X86_SUMMIT)  := mach-default

# default subarch .h files
mflags-y += -Iinclude/asm-i386/mach-default

head-y := arch/i386/kernel/head.o arch/i386/kernel/init_task.o

libs-y 					+= arch/i386/lib/
core-y					+= arch/i386/kernel/ \
					   arch/i386/mm/ \
					   arch/i386/$(mcore-y)/
drivers-$(CONFIG_MATH_EMULATION)	+= arch/i386/math-emu/
drivers-$(CONFIG_PCI)			+= arch/i386/pci/
# must be linked after kernel/
drivers-$(CONFIG_OPROFILE)		+= arch/i386/oprofile/

CFLAGS += $(mflags-y)
AFLAGS += $(mflags-y)

boot := arch/i386/boot

.PHONY: zImage bzImage compressed zlilo bzlilo \
	zdisk bzdisk fdimage fdimage144 fdimage288 install

all: bzImage

BOOTIMAGE=arch/i386/boot/bzImage
zImage zlilo zdisk: BOOTIMAGE=arch/i386/boot/zImage

zImage bzImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(BOOTIMAGE)

compressed: zImage

zlilo bzlilo: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(BOOTIMAGE) zlilo

zdisk bzdisk: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(BOOTIMAGE) zdisk

install fdimage fdimage144 fdimage288: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) BOOTIMAGE=$(BOOTIMAGE) $@

prepare: include/asm-$(ARCH)/asm_offsets.h
CLEAN_FILES += include/asm-$(ARCH)/asm_offsets.h

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/asm_offsets.h: arch/$(ARCH)/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)

archclean:
	$(Q)$(MAKE) $(clean)=arch/i386/boot

define archhelp
  echo  '* bzImage	- Compressed kernel image (arch/$(ARCH)/boot/bzImage)'
  echo  '  install	- Install kernel using'
  echo  '		   (your) ~/bin/installkernel or'
  echo  '		   (distribution) /sbin/installkernel or'
  echo  '		   install to $$(INSTALL_PATH) and run lilo'
  echo  '  bzdisk       - Create a boot floppy in /dev/fd0'
  echo  '  fdimage      - Create a boot floppy image'
endef

CLEAN_FILES += arch/$(ARCH)/boot/fdimage arch/$(ARCH)/boot/mtools.conf
