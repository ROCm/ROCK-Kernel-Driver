# BK Id: SCCS/s.Makefile 1.5 05/18/01 06:20:29 patch
#
#
# arch/ppc/mbxboot/Makefile
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

TFTPIMAGE	:= /tftpboot/zImage.embedded

OFFSET		:= ../utils/offset
SIZE		:= ../utils/size

LIBS		:= ../lib/zlib.a
OBJCOPY_ARGS	:= -O elf32-powerpc

ifdef CONFIG_8xx
ZLINKFLAGS	:= -T $(TOPDIR)/arch/$(ARCH)/vmlinux.lds -Ttext 0x00180000
OBJECTS		:= head.o m8xx_tty.o
CFLAGS		+= -DCONFIG_8xx
endif

ifdef CONFIG_8260
ZLINKFLAGS	:= -T $(TOPDIR)/arch/$(ARCH)/vmlinux.lds -Ttext 0x00400000
OBJECTS		:= head_8260.o m8260_tty.o embed_config.o
CFLAGS		+= -DCONFIG_8260
endif

OBJECTS		+= ../common/misc-common.o misc.o ../common/string.o
OBJCOPY_ARGS = -O elf32-powerpc

ifeq ($(CONFIG_MBX),y)
OBJECTS		+= iic.o embed_config.o pci.o qspan_pci.o
CFLAGS		+= -DCONFIG_MBX
endif
ifeq ($(CONFIG_RPXLITE),y)
CFLAGS		+= -DCONFIG_RPXLITE
OBJECTS		+= iic.o embed_config.o
endif
ifeq ($(CONFIG_RPXCLASSIC),y)
CFLAGS		+= -DCONFIG_RPXCLASSIC
OBJECTS		+= iic.o embed_config.o pci.o qspan_pci.o
endif
ifeq ($(CONFIG_BSEIP),y)
CFLAGS		+= -DCONFIG_BSEIP
OBJECTS		+= iic.o embed_config.o
endif
ifeq ($(CONFIG_FADS),y)
CFLAGS		+= -DCONFIG_FADS
OBJECTS		+= embed_config.o
endif

all:	zImage

misc.o: misc.c
	$(CC) $(CFLAGS) -DINITRD_OFFSET=0 -DINITRD_SIZE=0 -DZIMAGE_OFFSET=0 \
		-DZIMAGE_SIZE=0 -c -o $@ $*.c

zvmlinux.initrd: $(OBJECTS) $(LIBS) ../images/vmlinux.gz
	$(LD) $(ZLINKFLAGS) -o $@.tmp $(OBJECTS) $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=initrd=../images/ramdisk.image.gz \
		--add-section=image=../images/vmlinux.gz \
		$@.tmp $@
	$(CC) $(CFLAGS) -DINITRD_OFFSET=`sh $(OFFSET) $(OBJDUMP) $@ initrd` \
		-DINITRD_SIZE=`sh $(SIZE) $(OBJDUMP) $@ initrd` \
		-DZIMAGE_OFFSET=`sh $(OFFSET) $(OBJDUMP) $@ image` \
		-DZIMAGE_SIZE=`sh $(SIZE) $(OBJDUMP) $@ image` \
		-c -o misc.o misc.c
	$(LD) $(ZLINKFLAGS) -o $@.tmp $(OBJECTS) $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=initrd=../images/ramdisk.image.gz \
		--add-section=image=../images/vmlinux.gz \
		$@.tmp ../images/$@.mbx

zImage: zvmlinux
	ln -sf zvmlinux.mbx ../images/zImage.mbx

zImage.initrd: zvmlinux.initrd
	ln -sf zvmlinux.initrd.mbx ../images/zImage.initrd.mbx

zvmlinux: $(OBJECTS) $(LIBS) ../images/vmlinux.gz
#
# build the boot loader image and then compute the offset into it
# for the kernel image
#
	$(LD) $(ZLINKFLAGS) -o $@.tmp $(OBJECTS) $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=image=../images/vmlinux.gz \
		$@.tmp $@
#
# then with the offset rebuild the bootloader so we know where the kernel is
#
	$(CC) $(CFLAGS) -DINITRD_OFFSET=0 -DINITRD_SIZE=0 \
		-DZIMAGE_OFFSET=`sh $(OFFSET) $(OBJDUMP) $@ image` \
		-DZIMAGE_SIZE=`sh $(SIZE) $(OBJDUMP) $@ image` \
		-c -o misc.o misc.c
	$(LD) $(ZLINKFLAGS) -o $@.tmp $(OBJECTS) $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=image=../images/vmlinux.gz \
		$@.tmp ../images/$@.mbx
# Remove zvmlinux and zvmlinux.temp, we have ../images/zvmlinux.mbx
	rm -f $@.tmp $@

znetboot : zImage
	cp ../images/zImage.mbx $(TFTPIMAGE)

znetboot.initrd : zImage.initrd
	cp ../images/zImage.initrd.mbx $(TFTPIMAGE)

include $(TOPDIR)/Rules.make 
