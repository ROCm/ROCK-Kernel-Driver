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

boot: zImage

TFTPIMAGE			= /tftpboot/zImage.prep
ifeq ($(CONFIG_SMP),y)
TFTPIMAGE			= $(TFTPBOOT).smp
endif

LD_ARGS				= -T $(boot)/ld.script -Ttext 0x00800000 -Bstatic
OBJCOPY_ARGS			= -O elf32-powerpc
LIBS 				= $(common)/lib.a $(bootlib)/lib.a

boot-y				:= head.o misc.o
boot-$(CONFIG_VGA_CONSOLE)	+= vreset.o kbd.o

boot := arch/ppc/boot
common := $(boot)/common
utils := $(boot)/utils
bootlib := $(boot)/lib
of1275 := $(boot)/of1275
images := $(boot)/images
simple := $(boot)/simple

EXTRA_TARGETS	:= $(boot-y) ../simple/legacy.o
OBJS		:= $(addprefix $(obj)/,$(boot-y)) $(simple)/legacy.o 

# Tools
MKPREP				:= $(utils)/mkprep

# Extra include search dirs
CFLAGS_kbd.o			+= -Idrivers/char

zImage: $(images)/zImage.prep
zImage.initrd: $(images)/zImage.initrd.prep

$(obj)/dummy.o: $(common)/dummy.c
	$(CC) -c -o $@ $(common)/dummy.c

$(images)/zImage.prep: $(OBJS) $(LIBS) $(boot)/ld.script $(images)/vmlinux.gz \
		$(obj)/dummy.o $(MKPREP)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=.image=$(images)/vmlinux.gz \
		--set-section-flags=.image=contents,alloc,load,readonly,data \
		$(obj)/dummy.o $(obj)/image.o
	$(LD) $(LD_ARGS) -o $(obj)/zImage $(OBJS) $(obj)/image.o $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) $(obj)/zImage $(obj)/zImage \
		-R .comment -R .stab -R .stabstr
	$(MKPREP) -pbp $(obj)/zImage $@
	rm -f $(obj)/zImage

$(images)/zImage.initrd.prep: $(OBJS) $(LIBS) $(boot)/ld.script \
		$(images)/vmlinux.gz $(obj)/dummy.o $(MKPREP)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=.ramdisk=$(images)/ramdisk.image.gz \
		--set-section-flags=.ramdisk=contents,alloc,load,readonly,data \
		--add-section=.image=$(images)/vmlinux.gz \
		--set-section-flags=.image=contents,alloc,load,readonly,data \
		$(obj)/dummy.o $(obj)/image.o
	$(LD) $(LD_ARGS) -o $(obj)/zImage.initrd $(OBJS) $(obj)/image.o $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) $(obj)/zImage.initrd $(obj)/zImage.initrd \
		-R .comment -R .stab -R .stabstr
	$(MKPREP) -pbp $(obj)/zImage.initrd $@
	rm -f  $(obj)/zImage.initrd

floppy: zImage
	dd if=$(images)/zImage.prep of=/dev/fd0H1440 bs=64b

znetboot : zImage
	cp $(images)/zImage.prep $(TFTPIMAGE)

znetboot.initrd : zImage.initrd
	cp $(images)/zImage.initrd.prep $(TFTPIMAGE)
