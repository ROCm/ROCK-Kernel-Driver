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

CFLAGS += -pipe

# prevent gcc from keeping the stack 16 byte aligned
CFLAGS += $(shell if $(CC) -mpreferred-stack-boundary=2 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-mpreferred-stack-boundary=2"; fi)

cflags-$(CONFIG_M386)		+= -march=i386
cflags-$(CONFIG_M486)		+= -march=i486
cflags-$(CONFIG_M586)		+= -march=i586
cflags-$(CONFIG_M586TSC)	+= -march=i586
cflags-$(CONFIG_M586MMX)	+= -march=i586
cflags-$(CONFIG_M686)		+= -march=i686
cflags-$(CONFIG_MPENTIUMIII)	+= -march=i686
cflags-$(CONFIG_MPENTIUM4)	+= -march=i686
cflags-$(CONFIG_MK6)		+= $(shell if $(CC) -march=k6 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=k6"; else echo "-march=i586"; fi)
cflags-$(CONFIG_MK7)		+= $(shell if $(CC) -march=athlon -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=athlon"; else echo "-march=i686 -malign-functions=4"; fi) 
cflags-$(CONFIG_MCRUSOE)	+= -march=i686 -malign-functions=0 -malign-jumps=0 -malign-loops=0
cflags-$(CONFIG_MWINCHIPC6)	+= -march=i586
cflags-$(CONFIG_MWINCHIP2)	+= -march=i586
cflags-$(CONFIG_MWINCHIP3D)	+= -march=i586
cflags-$(CONFIG_MCYRIXIII)	+= -march=i586

CFLAGS += $(cflags-y)

ifdef CONFIG_VISWS
MACHINE	:= mach-visws
else
MACHINE	:= mach-generic
endif

HEAD := arch/i386/kernel/head.o arch/i386/kernel/init_task.o

libs-y 					+= arch/i386/lib/
core-y					+= arch/i386/kernel/ arch/i386/mm/ \
					   arch/i386/$(MACHINE)/
drivers-$(CONFIG_MATH_EMULATION)	+= arch/i386/math-emu/
drivers-$(CONFIG_PCI)			+= arch/i386/pci/

CFLAGS += -Iarch/i386/$(MACHINE)
AFLAGS += -Iarch/i386/$(MACHINE)

makeboot = $(call descend,arch/i386/boot,$(1))

.PHONY: zImage bzImage compressed zlilo bzlilo zdisk bzdisk install \
		clean archclean archmrproper

all: bzImage

BOOTIMAGE=arch/i386/boot/bzImage
zImage zlilo zdisk: BOOTIMAGE=arch/i386/boot/zImage

zImage bzImage: vmlinux
	@$(call makeboot,$(BOOTIMAGE))

compressed: zImage

zlilo bzlilo: vmlinux
	@$(call makeboot,BOOTIMAGE=$(BOOTIMAGE) zlilo)

zdisk bzdisk: vmlinux
	@$(call makeboot,BOOTIMAGE=$(BOOTIMAGE) zdisk)

install: vmlinux
	@$(call makeboot,BOOTIMAGE=$(BOOTIMAGE) install)

archclean:
	@$(call makeboot,clean)

archmrproper:
