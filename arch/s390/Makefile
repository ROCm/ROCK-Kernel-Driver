
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
LINKFLAGS =-T $(TOPDIR)/arch/s390/vmlinux.lds $(LDFLAGS)

CFLAGS_PIPE := -pipe
CFLAGS_NSR  := -fno-strength-reduce
CFLAGS := $(CFLAGS) $(CFLAGS_PIPE) $(CFLAGS_NSR)

HEAD := arch/s390/kernel/head.o arch/s390/kernel/init_task.o

SUBDIRS := $(SUBDIRS) arch/s390/mm arch/s390/kernel arch/s390/lib \
           drivers/s390
CORE_FILES := arch/s390/mm/mm.o arch/s390/kernel/kernel.o $(CORE_FILES) \
           drivers/s390/io.o
LIBS := $(TOPDIR)/arch/s390/lib/lib.a $(LIBS) $(TOPDIR)/arch/s390/lib/lib.a

all: image listing

listing: vmlinux
	@$(MAKEBOOT) listing

arch/s390/kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/s390/kernel

arch/s390/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/s390/mm

drivers/s390: dummy
	$(MAKE) linuxsubdirs SUBDIRS=drivers/s390

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

MAKESILO = $(MAKE) -C arch/$(ARCH)/tools/silo

MAKEDASDFMT = $(MAKE) -C arch/$(ARCH)/tools/dasdfmt

silo:
	@$(MAKESILO) silo

dasdfmt:
	@$(MAKEDASDFMT) dasdfmt

image: vmlinux 
	@$(MAKEBOOT) image

archclean:
	@$(MAKEBOOT) clean

archmrproper:

archdep:
	@$(MAKEBOOT) dep
