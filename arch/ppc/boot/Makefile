#
# arch/ppc/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Adapted for PowerPC by Gary Thomas
# modified by Cort (cort@cs.nmt.edu)
#

GZIP_FLAGS = -v9f

CFLAGS	+= -fno-builtin -D__BOOTER__ -I$(TOPDIR)/arch/$(ARCH)/boot/include

AFLAGS	+= -D__BOOTER__
OBJCOPY_ARGS = -O elf32-powerpc

ifeq ($(CONFIG_SMP),y)
TFTPSIMAGE=/tftpboot/sImage.smp
else
TFTPSIMAGE=/tftpboot/sImage
endif

lib/zlib.a:
	$(MAKE) -C lib

images/vmlinux.gz: $(TOPDIR)/vmlinux
	$(MAKE) -C images vmlinux.gz

# Subdirs and tools needed for each.
subdir-y			:= lib images common
subdir-$(CONFIG_ALL_PPC)	+= chrp pmac prep
tools-$(CONFIG_ALL_PPC)		:= addnote piggyback mknote hack-coff mkprep
subdir-$(CONFIG_4xx)		+= tree
subdir-$(CONFIG_8xx)		+= mbx
subdir-$(CONFIG_8260)		+= mbx
tools-$(CONFIG_GEMINI)		:= mksimage

# These are dirs we don't want to go into on BOOT_TARGETS
NONBOOT				:= lib images common

# These are the subdirs we want to use
BOOTDIRS			= $(filter-out $(NONBOOT), $(subdir-y))

# This will make the tools we need.  We do it like this to ensure that we use
# HOSTCC. -- Tom
maketools:
	$(MAKE) -C utils $(tools-y)

# The targets all boards support for boot images.
BOOT_TARGETS = zImage
ifndef CONFIG_GEMINI
BOOT_TARGETS += zImage.initrd znetboot znetboot.initrd
endif

$(BOOT_TARGETS): sImage vmapus lib/zlib.a images/vmlinux.gz maketools
ifneq ($(BOOTDIRS),)
	for d in $(BOOTDIRS); do $(MAKE) -C $$d $@; done
endif

sImage: $(TOPDIR)/vmlinux
ifdef CONFIG_GEMINI
	$(OBJCOPY) -I elf32-powerpc -O binary $(TOPDIR)/vmlinux images/sImage
endif

vmapus: $(TOPDIR)/vmlinux
ifdef CONFIG_APUS
	$(STRIP) $(TOPDIR)/vmlinux -o images/vmapus
	gzip $(GZIP_FLAGS) images/vmapus
endif

ifdef CONFIG_GEMINI
znetboot : zImage
	cp images/sImage $(TFTPSIMAGE)
endif

# Clean up after ourselves.  We have to do it like this since only some dirs
# need to be gone into. -- Tom
clean:
	$(MAKE) -C images clean
	$(MAKE) -C tree clean
	$(MAKE) -C utils clean

include $(TOPDIR)/Rules.make
