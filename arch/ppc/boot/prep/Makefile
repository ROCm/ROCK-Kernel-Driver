# BK Id: SCCS/s.Makefile 1.17 06/12/01 16:47:44 paulus
#
# arch/ppc/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Tom Rini	January 2001
#
# Originally:
# arch/ppc/boot/Makefile
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

ifeq ($(CONFIG_SMP),y)
TFTPIMAGE=/tftpboot/zImage.prep.smp
else
TFTPIMAGE=/tftpboot/zImage.prep
endif

ZLINKFLAGS = -T $(TOPDIR)/arch/$(ARCH)/vmlinux.lds -Ttext 0x00800000
OBJECTS := head.o misc.o ../common/misc-common.o \
		../common/string.o of1275.o
OBJCOPY_ARGS = -O elf32-powerpc
LIBS = ../lib/zlib.a

ifeq ($(CONFIG_SERIAL_CONSOLE),y)
OBJECTS += ns16550.o
endif

ifeq ($(CONFIG_VGA_CONSOLE),y)
OBJECTS += vreset.o kbd.o
endif

# Tools
MKPREP := ../utils/mkprep
SIZE := ../utils/size
OFFSET := ../utils/offset

all:	zImage

misc.o: misc.c
	$(CC) $(CFLAGS) -DINITRD_OFFSET=0 -DINITRD_SIZE=0 -DZIMAGE_OFFSET=0 \
		-DZIMAGE_SIZE=0 -c -o $@ $*.c

ns16550.o: ../common/ns16550.c
	$(CC) $(CFLAGS) -DIOOFFSET=0x80000000 -c -o $@ ../common/$*.c

zvmlinux.initrd: zvmlinux ../images/vmlinux.gz
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
		$@.tmp $@
	rm -f $@.tmp zvmlinux

zImage: zvmlinux $(MKPREP)
	$(MKPREP) -pbp zvmlinux ../images/$@.prep
	rm -f zvmlinux

zImage.initrd: zvmlinux.initrd $(MKPREP)
	$(MKPREP) -pbp zvmlinux.initrd ../images/$@.prep
	rm -f zvmlinux.initrd

zvmlinux: $(OBJECTS) $(LIBS) ../images/vmlinux.gz
#
# build the boot loader image and then compute the offset into it
# for the kernel image
#
	$(LD) $(ZLINKFLAGS) -o zvmlinux.tmp $(OBJECTS) $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=image=../images/vmlinux.gz zvmlinux.tmp $@
#
# then with the offset rebuild the bootloader so we know where the kernel is
#
	$(CC) $(CFLAGS) -DINITRD_OFFSET=0 -DINITRD_SIZE=0 \
		-DZIMAGE_OFFSET=`sh $(OFFSET) $(OBJDUMP) zvmlinux image` \
		-DZIMAGE_SIZE=`sh $(SIZE) $(OBJDUMP) zvmlinux image` \
		-c -o misc.o misc.c
	$(LD) $(ZLINKFLAGS) -o zvmlinux.tmp $(OBJECTS) $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=image=../images/vmlinux.gz $@.tmp $@
	rm $@.tmp

floppy: zImage
	dd if=../images/zImage.prep of=/dev/fd0H1440 bs=64b

znetboot : zImage
	cp ../images/zImage.prep $(TFTPIMAGE)

znetboot.initrd : zImage.initrd
	cp ../images/zImage.initrd.prep $(TFTPIMAGE)

include $(TOPDIR)/Rules.make
