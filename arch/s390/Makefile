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

LDFLAGS		:= -m elf_s390
OBJCOPYFLAGS	:= -O binary
LDFLAGS_vmlinux := -e start
LDFLAGS_BLOB	:= --format binary --oformat elf32-s390

CFLAGS += -pipe -fno-strength-reduce

HEAD := arch/s390/kernel/head.o arch/s390/kernel/init_task.o

core-y				+= arch/s390/mm/ arch/s390/kernel/
drivers-y			+= drivers/s390/
drivers-$(CONFIG_MATHEMU)	+= arch/s390/math-emu/
libs-y				+= arch/s390/lib/

all: image listing

makeboot = $(call descend,arch/$(ARCH)/boot,$(1))
BOOTIMAGE= arch/$(ARCH)/boot/image

listing install image: vmlinux
	+@$(call makeboot,BOOTIMAGE=$(BOOTIMAGE) $@)

archclean:
	+@$(call makeboot,clean)

archmrproper:

prepare: include/asm-$(ARCH)/offsets.h

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/offsets.h.tmp: arch/$(ARCH)/kernel/asm-offsets.s
	@$(generate-asm-offsets.h) < $< > $@

include/asm-$(ARCH)/offsets.h: include/asm-$(ARCH)/offsets.h.tmp
	@echo -n '  Generating $@'
	@$(update-if-changed)

CLEAN_FILES += include/asm-$(ARCH)/offsets.h.tmp \
	       include/asm-$(ARCH)/offsets.h
