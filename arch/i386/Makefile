#
# i386/Makefile
#
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
#
# 19990713  Artur Skawina <skawina@geocities.com>
#           Added '-march' and '-mpreferred-stack-boundary' support
#

LDFLAGS		:= -m elf_i386
OBJCOPYFLAGS	:= -O binary -R .note -R .comment -S
LDFLAGS_vmlinux := -e stext
LDFLAGS_BLOB	:= --format binary --oformat elf32-i386

CFLAGS += -pipe

check_gcc = $(shell if $(CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi)

# prevent gcc from keeping the stack 16 byte aligned
CFLAGS += $(call check_gcc,-mpreferred-stack-boundary=2,)

cflags-$(CONFIG_M386)		+= -march=i386
cflags-$(CONFIG_M486)		+= -march=i486
cflags-$(CONFIG_M586)		+= -march=i586
cflags-$(CONFIG_M586TSC)	+= -march=i586
cflags-$(CONFIG_M586MMX)	+= $(call check_gcc,-march=pentium-mmx,-march=i586)
cflags-$(CONFIG_M686)		+= -march=i686
cflags-$(CONFIG_MPENTIUMIII)	+= $(call check_gcc,-march=pentium3,-march=i686)
cflags-$(CONFIG_MPENTIUM4)	+= $(call check_gcc,-march=pentium4,-march=i686)
cflags-$(CONFIG_MK6)		+= $(call check_gcc,-march=k6,-march=i586)
cflags-$(CONFIG_MK7)		+= $(call check_gcc,-march=athlon,-march=i686 -malign-functions=4)
cflags-$(CONFIG_MCRUSOE)	+= -march=i686 -malign-functions=0 -malign-jumps=0 -malign-loops=0
cflags-$(CONFIG_MWINCHIPC6)	+= $(call check_gcc,-march=winchip-c6,-march=i586)
cflags-$(CONFIG_MWINCHIP2)	+= $(call check_gcc,-march=winchip2,-march=i586)
cflags-$(CONFIG_MWINCHIP3D)	+= -march=i586
cflags-$(CONFIG_MCYRIXIII)	+= $(call check_gcc,-march=c3,-march=i486)
# The alignment flags change with gcc 3.2
cflags-$(CONFIG_MCYRIXIII)	+= $(call check_gcc,-falign-functions=0 -falign-jumps=0 -falign-loops=0,-malign-functions=0 -malign-jumps=0 -malign-loops=0)

CFLAGS += $(cflags-y)

#default subarch .c files
mcore-y  := mach-default

#VISWS subarch support
mflags-$(CONFIG_VISWS) := -Iinclude/asm-i386/mach-visws
mcore-$(CONFIG_VISWS)  := mach-visws

#add other subarch support here

#default subarch .h files
mflags-y += -Iinclude/asm-i386/mach-default

HEAD := arch/i386/kernel/head.o arch/i386/kernel/init_task.o

libs-y 					+= arch/i386/lib/
core-y					+= arch/i386/kernel/ \
					   arch/i386/mm/ \
					   arch/i386/$(mcore-y)/
drivers-$(CONFIG_MATH_EMULATION)	+= arch/i386/math-emu/
drivers-$(CONFIG_PCI)			+= arch/i386/pci/
# FIXME: is drivers- right ?
drivers-$(CONFIG_OPROFILE)		+= arch/i386/oprofile/

CFLAGS += $(mflags-y)
AFLAGS += $(mflags-y)

makeboot =$(Q)$(MAKE) -f scripts/Makefile.build obj=arch/i386/boot $(1)

.PHONY: zImage bzImage compressed zlilo bzlilo zdisk bzdisk install \
		clean archclean archmrproper

all: bzImage

BOOTIMAGE=arch/i386/boot/bzImage
zImage zlilo zdisk: BOOTIMAGE=arch/i386/boot/zImage

zImage bzImage: vmlinux
	$(call makeboot,$(BOOTIMAGE))

compressed: zImage

zlilo bzlilo: vmlinux
	$(call makeboot,BOOTIMAGE=$(BOOTIMAGE) zlilo)

zdisk bzdisk: vmlinux
	$(call makeboot,BOOTIMAGE=$(BOOTIMAGE) zdisk)

install: vmlinux
	$(call makeboot,BOOTIMAGE=$(BOOTIMAGE) install)

archclean:
	$(Q)$(MAKE) -f scripts/Makefile.clean obj=arch/i386/boot

archmrproper:
