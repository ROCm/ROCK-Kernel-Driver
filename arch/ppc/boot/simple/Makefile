# This is far from simple, but I couldn't think of a good name.  This is
# for making the 'zImage' or 'zImage.initrd' on a number of targets.
#
# Author: Tom Rini <trini@mvista.com>
#
# 2001-2002 (c) MontaVista, Software, Inc.  This file is licensed under
# the terms of the GNU General Public License version 2.  This program
# is licensed "as is" without any warranty of any kind, whether express
# or implied.
#
# Notes:
# (1) For machine targets which produce more than one image, define
# ZNETBOOT and ZNETBOOTRD to the image which should be available for
# 'znetboot' and 'znetboot.initrd`
# (2) Also, for machine targets which just need to remove the ELF header,
# define END to be the machine name you want in the image.
# (3) For machine targets which use the mktree program, define END to be
# the machine name you want in the image, and you can optionally set
# ENTRYPOINT which the image should be loaded at.  The optimal setting
# for ENTRYPOINT is the link address.
# (4) It is advisable to pass in the memory size using BI_MEMSIZE and
# get_mem_size(), which is memory controller dependent.  Add in the correct
# XXX_memory.o file for this to work, as well as editing the $(MISC) file.

boot: zImage

boot				:= arch/ppc/boot
common				:= $(boot)/common
utils				:= $(boot)/utils
bootlib				:= $(boot)/lib
images				:= $(boot)/images

# Normally, we use the 'misc.c' file for decompress_kernel and
# whatnot.  Sometimes we need to override this however.
MISC				:= misc.o
ifeq ($(CONFIG_IBM_OPENBIOS),y)
ZIMAGE				:= zImage-TREE
ZIMAGEINITRD			:= zImage.initrd-TREE
END				:= treeboot
TFTPIMAGE			:= /tftpboot/zImage.$(END)
MISC				:= misc-embedded.o
endif
ifeq ($(CONFIG_EMBEDDEDBOOT),y)
TFTPIMAGE			:= /tftpboot/zImage.embedded
MISC				:= misc-embedded.o
endif
ifeq ($(CONFIG_EBONY),y)
ZIMAGE				:= zImage-TREE
ZIMAGEINITRD			:= zImage.initrd-TREE
EXTRA				:= direct.o
END				:= ebony
ENTRYPOINT			:= 0x01000000
TFTPIMAGE			:= /tftpboot/zImage.$(END)
endif
ifeq ($(CONFIG_EV64260),y)
EXTRA				:= direct.o misc-ev64260.o
TFTPIMAGE			:= /tftpboot/zImage.ev64260
endif
ifeq ($(CONFIG_GEMINI),y)
ZIMAGE				:= zImage-STRIPELF
ZIMAGEINITRD			:= zImage.initrd-STRIPELF
EXTRA				:= direct.o
END				:= gemini
TFTPIMAGE			:= /tftpboot/zImage.$(END)
endif
ifeq ($(CONFIG_MENF1),y)
ZIMAGE				:= zImage-MENF1
ZIMAGEINITRD			:= zImage.initrd-MENF1
EXTRA				:= chrpmap.o
TFTPIMAGE			:= /tftpboot/zImage.menf1
endif
ifeq ($(CONFIG_K2),y)
EXTRA				:= legacy.o
TFTPIMAGE			:= /tftpboot/zImage.k2
endif
# kbuild-2.4 'feature', only one of these will ever by 'y' at a time.
# The rest will be unset.
ifeq ($(CONFIG_MCPN765)$(CONFIG_MVME5100)$(CONFIG_PRPMC750)$(CONFIG_PRPMC800)$(CONFIG_LOPEC)$(CONFIG_PPLUS),y)
ZIMAGE				:= zImage-PPLUS
ZIMAGEINITRD			:= zImage.initrd-PPLUS
EXTRA				:= direct.o
TFTPIMAGE			:= /tftpboot/zImage.pplus
ZNETBOOT			:= zImage.pplus
ZNETBOOTRD			:= zImage.initrd.pplus
endif
ifeq ($(CONFIG_PPLUS),y)
EXTRA				:= legacy.o
endif
ifeq ($(CONFIG_PAL4),y)
EXTRA				:= direct.o
endif
ifeq ($(CONFIG_PCORE)$(CONFIG_POWERPMC250),y)
ZIMAGE				:= zImage-STRIPELF
ZIMAGEINITRD			:= zImage.initrd-STRIPELF
EXTRA				:= chrpmap.o
END				:= pcore
TFTPIMAGE			:= /tftpboot/zImage.$(END)
endif
# The PowerPMC 250 needs the dummy serial_fixups()
ifeq ($(CONFIG_POWERPMC250),y)
EXTRA				:= direct.o
endif
ifeq ($(CONFIG_SANDPOINT),y)
EXTRA				:= direct.o
TFTPIMAGE			:= /tftpboot/zImage.sandpoint
endif
ifeq ($(CONFIG_SPRUCE),y)
ZIMAGE				:= zImage-TREE
ZIMAGEINITRD			:= zImage.initrd-TREE
EXTRA				:= direct.o
END				:= spruce
ENTRYPOINT			:= 0x00800000
MISC				:= misc-spruce.o
TFTPIMAGE			:= /tftpboot/zImage.$(END)
endif
ifeq ($(CONFIG_ZX4500),y)
ZIMAGE				:= zImage-STRIPELF
ZIMAGEINITRD			:= zImage.initrd-STRIPELF
EXTRA				:= direct.o
END				:= zx4500
TFTPIMAGE			:= /tftpboot/zImage.$(END)
endif
ifeq ($(CONFIG_SMP),y)
TFTPIMAGE			+= .smp
endif
ifeq ($(CONFIG_REDWOOD_4),y)
# This is a treeboot that needs init functions until the
# boot rom is sorted out (i.e. this is short lived)
EXTRA_AFLAGS			:= -Wa,-m405
EXTRA				:= rw4/rw4_init.o rw4/rw4_init_brd.o
endif

# Linker args.  This specifies where the image will be run at.
LD_ARGS				= -T $(boot)/ld.script \
					-Ttext $(CONFIG_BOOT_LOAD) -Bstatic
OBJCOPY_ARGS			:= -O elf32-powerpc

# head.o and relocate.o must be at the start.
boot-y				:= head.o relocate.o $(EXTRA) $(MISC) 
boot-$(CONFIG_40x)		+= embed_config.o
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
boot-$(CONFIG_GT64260_CONSOLE)	+= gt64260_tty.o
endif

LIBS				:= $(common)/lib.a $(bootlib)/lib.a

OBJS				:= $(addprefix $(obj)/,$(boot-y))

# Tools
MKBUGBOOT			:= $(utils)/mkbugboot
MKPREP				:= $(utils)/mkprep
MKTREE				:= $(utils)/mktree

targets := dummy.o

$(obj)/zvmlinux: $(OBJS) $(LIBS) $(boot)/ld.script $(images)/vmlinux.gz \
		$(obj)/dummy.o
	$(OBJCOPY) $(OBJCOPY_ARGS) \
		--add-section=.image=$(images)/vmlinux.gz \
		--set-section-flags=.image=contents,alloc,load,readonly,data \
		$(obj)/dummy.o $(obj)/image.o
	$(LD) $(LD_ARGS) -o $@ $(OBJS) $(obj)/image.o $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) $@ $@ -R .comment -R .stab \
		-R .stabstr -R .ramdisk -R .sysmap

$(obj)/zvmlinux.initrd: $(OBJS) $(LIBS) $(boot)/ld.script \
		$(images)/vmlinux.gz $(obj)/dummy.o
	$(OBJCOPY) $(OBJCOPY_ARGS) \
		--add-section=.ramdisk=$(images)/ramdisk.image.gz \
		--set-section-flags=.ramdisk=contents,alloc,load,readonly,data \
		--add-section=.image=$(images)/vmlinux.gz \
		--set-section-flags=.image=contents,alloc,load,readonly,data \
		$(obj)/dummy.o $(obj)/image.o
	$(LD) $(LD_ARGS) -o $@ $(OBJS) $(obj)/image.o $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) $@ $@ -R .comment -R .stab \
		-R .stabstr -R .sysmap

# Sort-of dummy rules, that let us format the image we want.
zImage: $(images)/$(ZIMAGE) $(obj)/zvmlinux
	cp -f $(obj)/zvmlinux $(images)/zImage.elf
	rm -f $(obj)/zvmlinux

zImage.initrd: $(images)/$(ZIMAGEINITRD) $(obj)/zvmlinux.initrd
	cp -f $(obj)/zvmlinux.initrd $(images)/zImage.initrd.elf
	rm -f $(obj)/zvmlinux.initrd

znetboot: zImage
ifneq ($(ZNETBOOT),)
	cp $(images)/$(ZNETBOOT) $(TFTPIMAGE)
else
	cp $(images)/zImage.* $(TFTPIMAGE)
endif

znetboot.initrd: zImage.initrd
ifneq ($(ZNETBOOTRD),)
	cp $(images)/$(ZNETBOOTRD) $(TFTPIMAGE)
else
	cp $(images)/zImage.* $(TFTPIMAGE)
endif

$(images)/zImage-STRIPELF: $(obj)/zvmlinux
	dd if=$(obj)/zvmlinux of=$(images)/zImage.$(END) skip=64 bs=1k

$(images)/zImage.initrd-STRIPELF: $(obj)/zvmlinux.initrd
	dd if=$(obj)/zvmlinux.initrd of=$(images)/zImage.initrd.$(END) \
		skip=64 bs=1k

$(images)/zImage-TREE: $(obj)/zvmlinux $(MKTREE)
	$(MKTREE) $(obj)/zvmlinux $(images)/zImage.$(END) $(ENTRYPOINT)

$(images)/zImage.initrd-TREE: $(obj)/zvmlinux.initrd $(MKTREE)
	$(MKTREE) $(obj)/zvmlinux.initrd $(images)/zImage.initrd.$(END) \
		$(ENTRYPOINT)

$(images)/zImage-MENF1: $(obj)/zvmlinux $(MKPREP)
	$(MKPREP) -pbp $(obj)/zvmlinux $(images)/zImage.menf1

$(images)/zImage.initrd-MENF1: $(obj)/zvmlinux.initrd $(MKPREP)
	$(MKPREP) -pbp $(obj)/zvmlinux.initrd $(images)/zImage.initrd.menf1

$(images)/zImage-PPLUS: $(obj)/zvmlinux $(MKPREP) $(MKBUGBOOT)
	$(MKPREP) -pbp $(obj)/zvmlinux $(images)/zImage.pplus
	$(MKBUGBOOT) $(obj)/zvmlinux $(images)/zImage.bugboot

$(images)/zImage.initrd-PPLUS: $(obj)/zvmlinux.initrd $(MKPREP) $(MKBUGBOOT)
	$(MKPREP) -pbp $(obj)/zvmlinux.initrd $(images)/zImage.initrd.pplus
	$(MKBUGBOOT) $(obj)/zvmlinux.initrd $(images)/zImage.initrd.bugboot
