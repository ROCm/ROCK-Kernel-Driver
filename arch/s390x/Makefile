
# s390x/Makefile
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

LDFLAGS		:= -m elf64_s390
CPP=$(CC) -E
OBJCOPY=$(CROSS_COMPILE)objcopy -O binary -R .note -R .comment -S
ifeq ($(CONFIG_SHARED_KERNEL),y)
  LINKSCRIPT := arch/s390x/vmlinux-shared.lds
else
  LINKSCRIPT := arch/s390x/vmlinux.lds
endif
LDFLAGS_vmlinux := -T $(LINKSCRIPT) -e start
MODFLAGS += -fpic

CFLAGS_PIPE := -pipe
CFLAGS_NSR  := -fno-strength-reduce
CFLAGS := $(CFLAGS) $(CFLAGS_PIPE) $(CFLAGS_NSR)

HEAD := arch/s390x/kernel/head.o arch/s390x/kernel/init_task.o

SUBDIRS += arch/s390x/mm arch/s390x/kernel arch/s390x/lib drivers/s390
CORE_FILES := arch/s390x/mm/mm.o arch/s390x/kernel/kernel.o $(CORE_FILES)
DRIVERS := $(DRIVERS) drivers/s390/built-in.o
LIBS := $(TOPDIR)/arch/s390x/lib/lib.a $(LIBS) $(TOPDIR)/arch/s390x/lib/lib.a

all: image listing

listing: vmlinux
	@$(MAKEBOOT) listing

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

vmlinux: $(LINKSCRIPT)

image: vmlinux 
	@$(MAKEBOOT) image

install: vmlinux
	@$(MAKEBOOT) BOOTIMAGE=image install

archclean:
	@$(MAKEBOOT) clean
	$(MAKE) -C arch/$(ARCH)/kernel clean

archmrproper:
