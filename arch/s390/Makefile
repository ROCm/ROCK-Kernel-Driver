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

LD=$(CROSS_COMPILE)ld -m elf_s390
OBJCOPY=$(CROSS_COMPILE)objcopy -O binary -R .note -R .comment -S
LDFLAGS=-e start
ifeq ($(CONFIG_SHARED_KERNEL),y)
  LINKSCRIPT := arch/s390/vmlinux-shared.lds
else
  LINKSCRIPT := arch/s390/vmlinux.lds
endif
LINKFLAGS =-T $(TOPDIR)/$(LINKSCRIPT) $(LDFLAGS)

CFLAGS_PIPE := -pipe
CFLAGS_NSR  := -fno-strength-reduce
CFLAGS := $(CFLAGS) $(CFLAGS_PIPE) $(CFLAGS_NSR)

HEAD := arch/s390/kernel/head.o arch/s390/kernel/init_task.o

SUBDIRS += arch/s390/mm arch/s390/kernel arch/s390/lib drivers/s390
CORE_FILES := arch/s390/mm/mm.o arch/s390/kernel/kernel.o $(CORE_FILES)
LIBS := $(TOPDIR)/arch/s390/lib/lib.a $(LIBS) $(TOPDIR)/arch/s390/lib/lib.a

DRIVERS += drivers/s390/built-in.o

ifeq ($(CONFIG_MATHEMU),y)
  SUBDIRS += arch/s390/math-emu
  DRIVERS += arch/s390/math-emu/math-emu.o
endif

all: image listing

listing: vmlinux
	@$(MAKEBOOT) listing

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

vmlinux: $(LINKSCRIPT)

image: vmlinux 
	@$(MAKEBOOT) image

archclean:
	@$(MAKEBOOT) clean
	$(MAKE) -C arch/$(ARCH)/kernel clean

archmrproper:

install: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=image install
