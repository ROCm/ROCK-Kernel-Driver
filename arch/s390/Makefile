#
# s390/Makefile
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

ifdef CONFIG_ARCH_S390_31
LDFLAGS		:= -m elf_s390
LDFLAGS_BLOB	:= --format binary --oformat elf32-s390
CFLAGS		+= -m31
UTS_MACHINE	:= s390
endif

ifdef CONFIG_ARCH_S390X
LDFLAGS		:= -m elf64_s390
MODFLAGS	+= -fpic -D__PIC__
LDFLAGS_BLOB	:= --format binary --oformat elf64-s390
CFLAGS		+= -m64
UTS_MACHINE	:= s390x
endif

OBJCOPYFLAGS	:= -O binary
LDFLAGS_vmlinux := -e start
CFLAGS 		+= -pipe -fno-strength-reduce -finline-limit-10000 -Wno-sign-compare 

head-$(CONFIG_ARCH_S390_31)	+= arch/$(ARCH)/kernel/head.o
head-$(CONFIG_ARCH_S390X)	+= arch/$(ARCH)/kernel/head64.o
head-y				+= arch/$(ARCH)/kernel/init_task.o

core-y		+= arch/$(ARCH)/mm/ arch/$(ARCH)/kernel/
libs-y		+= arch/$(ARCH)/lib/
drivers-y	+= drivers/s390/
drivers-$(CONFIG_MATHEMU) += arch/$(ARCH)/math-emu/


makeboot =$(Q)$(MAKE) -f scripts/Makefile.build obj=arch/$(ARCH)/boot $(1)

all: image listing

listing image: vmlinux
	$(call makeboot,arch/$(ARCH)/boot/$@)

install: vmlinux
	$(call makeboot, $@)

archclean:
	$(Q)$(MAKE) -f scripts/Makefile.clean obj=arch/$(ARCH)/boot


prepare: include/asm-$(ARCH)/offsets.h

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/offsets.h: arch/$(ARCH)/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)

CLEAN_FILES += include/asm-$(ARCH)/offsets.h
