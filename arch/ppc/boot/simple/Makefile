# This is far from simple, but I couldn't think of a good name.  This is
# for making the 'zImage' or 'zImage.initrd' on a number of targets.
#
# Author: Tom Rini <trini@mvista.com>
#
# Copyright 2001 MontaVista Software Inc.
#
# Notes: For machine targets which produce more than one image, define
# ZNETBOOT and ZNETBOOTRD to the image which should be available for
# 'znetboot' and 'znetboot.initrd`
#
# This program is free software; you can redistribute  it and/or modify it
# under  the terms of  the GNU General  Public License as published by the
# Free Software Foundation;  either version 2 of the  License, or (at your
# option) any later version.

# Normally, we use the 'misc-simple.c' file for decompress_kernel and
# whatnot.  Sometimes we need to override this however.
MISC				:= ../common/misc-simple.o
ifeq ($(CONFIG_TREEBOOT),y)
ZIMAGE				:= zImage-TREE
ZIMAGEINITRD			:= zImage.initrd-TREE
TFTPIMAGE			:= /tftpboot/zImage.embedded
MISC				:= misc-embedded.o
endif
ifeq ($(CONFIG_EMBEDDEDBOOT),y)
ZIMAGE				:= zImage-EMBEDDED
ZIMAGEINITRD			:= zImage.initrd-EMBEDDED
TFTPIMAGE			:= /tftpboot/zImage.embedded
MISC				:= misc-embedded.o
endif
ifeq ($(CONFIG_EV64260),y)
ZIMAGE				:= zImage-EV64260
ZIMAGEINITRD			:= zImage.initrd-EV64260
HEADHELP			:= direct.o misc-ev64260.o
TFTPIMAGE			:= /tftpboot/zImage.ev64260
endif
ifeq ($(CONFIG_GEMINI),y)
ZIMAGE				:= zImage-SMON
ZIMAGEINITRD			:= zImage.initrd-SMON
HEADHELP			:= direct.o
TFTPIMAGE			:= /tftpboot/zImage.gemini
endif
ifeq ($(CONFIG_MENF1),y)
ZIMAGE				:= zImage-MENF1
ZIMAGEINITRD			:= zImage.initrd-MENF1
HEADHELP			:= chrpmap.o
TFTPIMAGE			:= /tftpboot/zImage.menf1
endif
ifeq ($(CONFIG_K2),y)
ZIMAGE				:= zImage-K2
ZIMAGEINITRD			:= zImage.initrd-K2
HEADHELP			:= legacy.o
TFTPIMAGE			:= /tftpboot/zImage.k2
endif
# kbuild-2.4 'feature', only one of these will ever by 'y' at a time.
# The rest will be unset.
ifeq ($(CONFIG_MCPN765)$(CONFIG_MVME5100)$(CONFIG_PRPMC750)$(CONFIG_PRPMC800)$(CONFIG_LOPEC)$(CONFIG_PPLUS),y)
ZIMAGE				:= zImage-PPLUS
ZIMAGEINITRD			:= zImage.initrd-PPLUS
HEADHELP			:= direct.o
TFTPIMAGE			:= /tftpboot/zImage.pplus
ZNETBOOT			:= zImage.pplus
ZNETBOOTRD			:= zImage.initrd.pplus
endif
ifeq ($(CONFIG_PPLUS),y)
HEADHELP			:= legacy.o
endif
ifeq ($(CONFIG_PCORE),y)
ZIMAGE				:= zImage-PCORE
ZIMAGEINITRD			:= zImage.initrd-PCORE
HEADHELP			:= chrpmap.o
TFTPIMAGE			:= /tftpboot/zImage.pcore
endif
#Ugh, should come up with a better nameing convention..
ifeq ($(CONFIG_POWERPMC250),y)
ZIMAGE				:= zImage-PCORE
ZIMAGEINITRD			:= zImage.initrd-PCORE
HEADHELP			:= direct.o
TFTPIMAGE			:= /tftpboot/zImage.pcore
endif
ifeq ($(CONFIG_SANDPOINT),y)
ZIMAGE				:= zImage-SP
ZIMAGEINITRD			:= zImage.initrd-SP
HEADHELP			:= direct.o
TFTPIMAGE			:= /tftpboot/zImage.sandpoint
endif
ifeq ($(CONFIG_SPRUCE),y)
ZIMAGE				:= zImage-SPRUCE
ZIMAGEINITRD			:= zImage.initrd-SPRUCE
HEADHELP			:= direct.o
MISC				:= misc-spruce.o
TFTPIMAGE			:= /tftpboot/zImage.spruce
endif
ifeq ($(CONFIG_ZX4500),y)
ZIMAGE				:= zImage-ZX4500
ZIMAGEINITRD			:= zImage.initrd-ZX4500
HEADHELP			:= direct.o
TFTPIMAGE			:= /tftpboot/zImage.zx4500
endif
ifeq ($(CONFIG_SMP),y)
TFTPIMAGE			+= .smp
endif
ifeq ($(CONFIG_REDWOOD_4),y)
# This is a treeboot that needs init functions until the
# boot rom is sorted out (i.e. this is short lived)
EXTRA_AFLAGS			:= -Wa,-m405
HEADHELP			:= rw4/rw4_init.o rw4/rw4_init_brd.o
endif

# Default linker args.  Link at 0x00800000 or 0x00400000 by default, but
# allow it to be overridden.
ifeq ($(CONFIG_BOOT_LOAD_BOOL),y)
LD_ARGS				:= -T ../ld.script -Ttext $(CONFIG_BOOT_LOAD) \
					-Bstatic
else
LD_ARGS				= -T ../ld.script -Ttext 0x00800000 -Bstatic
ifeq ($(CONFIG_8260)$(CONFIG_4xx)$(CONFIG_8xx),y)
LD_ARGS				:= -T ../ld.script -Ttext 0x00400000 -Bstatic
endif
endif
OBJCOPY_ARGS			:= -O elf32-powerpc

# head.o and ../common/relocate.o must be at the start.
boot-y				:= head.o ../common/relocate.o $(HEADHELP) \
				$(MISC) ../common/misc-common.o \
				../common/string.o ../common/util.o
boot-$(CONFIG_4xx)		+= embed_config.o
boot-$(CONFIG_8xx)		+= embed_config.o
boot-$(CONFIG_8260)		+= embed_config.o
boot-$(CONFIG_BSEIP)		+= iic.o
boot-$(CONFIG_MBX)		+= iic.o pci.o qspan_pci.o
boot-$(CONFIG_RPXCLASSIC)	+= iic.o pci.o qspan_pci.o
boot-$(CONFIG_RPXLITE)		+= iic.o
# Different boards need different serial implementations.
ifeq ($(CONFIG_SERIAL_CONSOLE),y)
boot-$(CONFIG_8xx)		+= m8xx_tty.o
boot-$(CONFIG_8260)		+= m8260_tty.o
boot-$(CONFIG_GT64260_CONSOLE)   += gt64260_tty.o
boot-$(CONFIG_SERIAL)		+= ../common/ns16550.o
endif

EXTRA_TARGETS			:= $(boot-y)
LIBS				:= ../lib/zlib.a

# Tools
MKBUGBOOT			:= ../utils/mkbugboot
MKPREP				:= ../utils/mkprep
MKTREE				:= ../utils/mktree

zvmlinux: $(boot-y) $(LIBS) ../ld.script ../images/vmlinux.gz ../common/dummy.o
	$(OBJCOPY) $(OBJCOPY_ARGS) \
		--add-section=.image=../images/vmlinux.gz \
		--set-section-flags=.image=contents,alloc,load,readonly,data \
		../common/dummy.o image.o
	$(LD) $(LD_ARGS) -o $@ $(boot-y) image.o $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) $@ $@ -R .comment -R .stab -R .stabstr \
		-R .ramdisk -R .sysmap

zvmlinux.initrd: $(boot-y) $(LIBS) ../ld.script ../images/vmlinux.gz \
		../common/dummy.o
	$(OBJCOPY) $(OBJCOPY_ARGS) \
		--add-section=.ramdisk=../images/ramdisk.image.gz \
		--set-section-flags=.ramdisk=contents,alloc,load,readonly,data \
		--add-section=.image=../images/vmlinux.gz \
		--set-section-flags=.image=contents,alloc,load,readonly,data \
		../common/dummy.o image.o
	$(LD) $(LD_ARGS) -o $@ $(boot-y) image.o $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) $@ $@ -R .comment -R .stab -R .stabstr \
		-R .sysmap

# Sort-of dummy rules, that let us format the image we want.
zImage: $(ZIMAGE)
	rm -f zvmlinux
zImage.initrd: $(ZIMAGEINITRD)
	rm -f zvmlinux.initrd

znetboot: zImage
ifneq ($(ZNETBOOT),)
	cp ../images/$(ZNETBOOT) $(TFTPIMAGE)
else
	cp ../images/zImage.* $(TFTPIMAGE)
endif

znetboot.initrd: zImage.initrd
ifneq ($(ZNETBOOTRD),)
	cp ../images/$(ZNETBOOTRD) $(TFTPIMAGE)
else
	cp ../images/zImage.* $(TFTPIMAGE)
endif

zImage-EMBEDDED: zvmlinux
	mv zvmlinux ../images/zImage.embedded

zImage.initrd-EMBEDDED: zvmlinux.initrd
	mv zvmlinux.initrd ../images/zImage.initrd.embedded

zImage-K2: zvmlinux 
	mv zvmlinux ../images/zImage.k2

zImage.initrd-K2: zvmlinux.initrd 
	mv zvmlinux.initrd ../images/zImage.initrd.k2

zImage-EV64260: zvmlinux
	mv zvmlinux ../images/zImage.ev64260

zImage.initrd-EV64260: zvmlinux.initrd
	mv zvmlinux.initrd ../images/zImage.initrd.ev64260

zImage-MENF1: zvmlinux
	$(MKPREP) -pbp zvmlinux ../images/zImage.menf1

zImage.initrd-MENF1: zvmlinux.initrd
	$(MKPREP) -pbp zvmlinux.initrd ../images/zImage.initrd.menf1

zImage-PCORE: zvmlinux
	dd if=zvmlinux of=../images/zImage.pcore skip=64 bs=1k

zImage.initrd-PCORE: zvmlinux.initrd
	dd if=zvmlinux.initrd of=../images/zImage.initrd.pcore skip=64 bs=1k

zImage-PPLUS: zvmlinux $(MKPREP) $(MKBUGBOOT)
	$(MKPREP) -pbp zvmlinux ../images/zImage.pplus
	$(MKBUGBOOT) zvmlinux ../images/zImage.bugboot

zImage.initrd-PPLUS: zvmlinux.initrd $(MKPREP) $(MKBUGBOOT)
	$(MKPREP) -pbp zvmlinux.initrd ../images/zImage.initrd.pplus
	$(MKBUGBOOT) zvmlinux.initrd ../images/zImage.initrd.bugboot

zImage-SMON: zvmlinux
	dd if=zvmlinux of=../images/zImage.gemini skip=64 bs=1k

zImage.initrd-SMON: zvmlinux.initrd
	dd if=zvmlinux.initrd of=../images/zImage.initrd.gemini skip=64 bs=1k

zImage-SP: zvmlinux
	mv zvmlinux ../images/zImage.sandpoint

zImage.initrd-SP: zvmlinux.initrd
	mv zvmlinux.initrd ../images/zImage.initrd.sandpoint

zImage-SPRUCE: zvmlinux
	$(MKTREE) zvmlinux ../images/zImage.spruce 0x800000

zImage.initrd-SPRUCE: zvmlinux.initrd
	$(MKTREE) zvmlinux.initrd ../images/zImage.initrd.spruce 0x800000

zImage-TREE: zvmlinux
	$(MKTREE) zvmlinux ../images/zImage.treeboot

zImage.initrd-TREE: zvmlinux.initrd
	$(MKTREE) zvmlinux.initrd ../images/zImage.initrd.treeboot

zImage-ZX4500: zvmlinux
	dd if=zvmlinux of=../images/zImage.zx4500 skip=64 bs=1k

zImage.initrd-ZX4500: zvmlinux.initrd
	dd if=zvmlinux.initrd of=../images/zImage.initrd.zx4500 skip=64 bs=1k

include $(TOPDIR)/Rules.make
