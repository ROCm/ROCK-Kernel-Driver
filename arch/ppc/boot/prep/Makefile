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

TFTPIMAGE		:= /tftpboot/zImage.prep
ifeq ($(CONFIG_SMP),y)
TFTPIMAGE		:= $(TFTPIMAGE).smp
endif

boot-y				:= head.o misc.o
boot-$(CONFIG_VGA_CONSOLE)	+= vreset.o kbd.o

boot	:= arch/ppc/boot
common	:= $(boot)/common
utils	:= $(boot)/utils
bootlib	:= $(boot)/lib
images	:= $(boot)/images
simple	:= $(boot)/simple

OBJCOPYFLAGS	:= -O elf32-powerpc
LIBS 		:= $(common)/lib.a $(bootlib)/lib.a

targets		:= $(boot-y) dummy.o ../simple/legacy.o
OBJS		:= $(addprefix $(obj)/,$(boot-y)) $(simple)/legacy.o

# Extra include search dirs
CFLAGS_kbd.o			+= -Idrivers/char

zImage: initrd :=
zImage: $(images)/zImage.prep
	@echo '  kernel: $@ is ready ($<)'

zImage.initrd: initrd := .initrd
zImage.initrd: $(images)/zImage.initrd.prep
	@echo '  kernel: $@ is ready ($<)'

$(images)/ramdisk.image.gz:
	@echo '  MISSING $@'
	@echo '          RAM disk image must be provided separately'
	@/bin/false

OBJCOPYFLAGS_image.o   := \
	--add-section=.image=$(images)/vmlinux.gz \
	--set-section-flags=.image=contents,alloc,load,readonly,data
targets	+= image.o
$(obj)/image.o: $(obj)/dummy.o $(images)/vmlinux.gz
	$(call if_changed,objcopy)

OBJCOPYFLAGS_image.initrd.o    := \
	--add-section=.ramdisk=$(images)/ramdisk.image.gz \
	--set-section-flags=.ramdisk=contents,alloc,load,readonly,data \
	--add-section=.image=$(images)/vmlinux.gz \
	--set-section-flags=.image=contents,alloc,load,readonly,data
targets	+= image.initrd.o
$(obj)/image.initrd.o: $(obj)/dummy.o $(images)/vmlinux.gz $(images)/ramdisk.image.gz
	$(call if_changed,objcopy)

LDFLAGS_zImage.bin		:= -Ttext 0x00800000 -Bstatic -T
LDFLAGS_zImage.initrd.bin	:= -Ttext 0x00800000 -Bstatic -T
targets += zImage.bin
$(obj)/zImage.bin: $(boot)/ld.script $(OBJS) $(obj)/image.o $(LIBS)
	$(call if_changed,ld)
targets += zImage.initrd.bin
$(obj)/zImage.initrd.bin: $(boot)/ld.script $(OBJS) $(obj)/image.initrd.o $(LIBS)
	$(call if_changed,ld)

OBJCOPYFLAGS_zImage		:= -R .comment -R .stab -R .stabstr
OBJCOPYFLAGS_zImage.initrd	:= -R .comment -R .stab -R .stabstr
targets += zImage
$(obj)/zImage: %: %.bin FORCE
	$(call if_changed,objcopy)
targets += zImage.initrd
$(obj)/zImage.initrd: %: %.bin FORCE
	$(call if_changed,objcopy)

quiet_cmd_mkprep = MKPREP  $@
      cmd_mkprep = $(utils)/mkprep -pbp $< $@

$(images)/zImage.prep: $(obj)/zImage $(utils)/mkprep
	$(call cmd,mkprep)
$(images)/zImage.initrd.prep: $(obj)/zImage.initrd $(utils)/mkprep
	$(call cmd,mkprep)

#
# Convinient shorthands for various targets
#
floppy: zImage
	dd if=$(images)/zImage.prep of=/dev/fd0H1440 bs=64b

znetboot : zImage
	cp $(images)/zImage.prep $(TFTPIMAGE)
	@echo '  kernel: $@ is ready ($<)'

znetboot.initrd : zImage.initrd
	cp $(images)/zImage.initrd.prep $(TFTPIMAGE)
	@echo '  kernel: $@ is ready ($<)'

