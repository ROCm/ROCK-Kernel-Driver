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

.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $<
.S.s:
	$(CPP) $(AFLAGS) -traditional -o $*.o $<
.S.o:
	$(CC) $(AFLAGS) -traditional -c -o $*.o $<

GZIP_FLAGS = -v9f

CFLAGS	:= $(CPPFLAGS) -O2 -DSTDC_HEADERS -fno-builtin -D__BOOTER__ \
		-I$(TOPDIR)/arch/$(ARCH)/boot/include
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

# Since gemini doesn't need/have it's own directory, we do znetboot* here
ifdef CONFIG_GEMINI
BOOT_TARGETS = zImage zImage.initrd
else
BOOT_TARGETS = zImage zImage.initrd znetboot znetboot.initrd
endif

# We go into the utils dir by hand to ensure HOSTCC builds
$(BOOT_TARGETS): sImage vmapus lib/zlib.a images/vmlinux.gz
ifneq ("xx$(CONFIG_8260)$(CONFIG_8xx)","xx")
	$(MAKE) -C mbx $@
endif
ifdef CONFIG_ALL_PPC
	$(MAKE) -C utils addnote piggyback mknote hack-coff mkprep
	$(MAKE) -C chrp $@
	$(MAKE) -C pmac $@
	$(MAKE) -C prep $@
endif
ifdef CONFIG_4xx
	$(MAKE) -C tree $@
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

# Do the dirs
clean:
	$(MAKE) -C images clean
	$(MAKE) -C tree clean
	$(MAKE) -C utils clean

dep:
	$(MAKE) -C mbx fastdep
	$(MAKE) -C chrp fastdep
	$(MAKE) -C common fastdep
	$(MAKE) -C pmac fastdep
	$(MAKE) -C prep fastdep
	$(MAKE) -C common fastdep

include $(TOPDIR)/Rules.make
