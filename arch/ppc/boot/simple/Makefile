# This is far from simple, but I couldn't think of a good name.  This is
# for making the 'zImage' or 'zImage.initrd' on a number of targets.
#
# Author: Tom Rini <trini@mvista.com>
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


boot				:= arch/ppc/boot
common				:= $(boot)/common
utils				:= $(boot)/utils
bootlib				:= $(boot)/lib
images				:= $(boot)/images

# Normally, we use the 'misc.c' file for decompress_kernel and
# whatnot.  Sometimes we need to override this however.
misc-y	:= misc.o

# Additionally, we normally don't need to mess with the L2 / L3 caches
# if present on 'classic' PPC.
cacheflag-y	:= -DCLEAR_CACHES=""
# This file will flush / disable the L2, and L3 if present.
clear_L2_L3	:= $(boot)/simple/clear.S

#
# See arch/ppc/kconfig and arch/ppc/platforms/Kconfig
# for definition of what platform each config option refer to.
#----------------------------------------------------------------------------
      zimage-$(CONFIG_IBM_OPENBIOS)	:= zImage-TREE
zimageinitrd-$(CONFIG_IBM_OPENBIOS)	:= zImage.initrd-TREE
         end-$(CONFIG_IBM_OPENBIOS)	:= treeboot
   tftpimage-$(CONFIG_IBM_OPENBIOS)	:= /tftpboot/zImage.$(end-y)
        misc-$(CONFIG_IBM_OPENBIOS)	:= misc-embedded.o

   tftpimage-$(CONFIG_EMBEDDEDBOOT)	:=  /tftpboot/zImage.embedded
        misc-$(CONFIG_EMBEDDEDBOOT)	:= misc-embedded.o

      zimage-$(CONFIG_EBONY)		:= zImage-TREE
zimageinitrd-$(CONFIG_EBONY)		:= zImage.initrd-TREE
         end-$(CONFIG_EBONY)		:= ebony
  entrypoint-$(CONFIG_EBONY)		:= 0x01000000
   tftpimage-$(CONFIG_EBONY)		:= /tftpboot/zImage.$(end-y)

      zimage-$(CONFIG_OCOTEA)		:= zImage-TREE
zimageinitrd-$(CONFIG_OCOTEA)		:= zImage.initrd-TREE
         end-$(CONFIG_OCOTEA)		:= ocotea
  entrypoint-$(CONFIG_OCOTEA)		:= 0x01000000
   tftpimage-$(CONFIG_OCOTEA)		:= /tftpboot/zImage.$(end-y)

     extra.o-$(CONFIG_EV64260)		:= direct.o misc-ev64260.o
   tftpimage-$(CONFIG_EV64260)		:= /tftpboot/zImage.ev64260
   cacheflag-$(CONFIG_EV64260)		:= -include $(clear_L2_L3)

      zimage-$(CONFIG_GEMINI)		:= zImage-STRIPELF
zimageinitrd-$(CONFIG_GEMINI)		:= zImage.initrd-STRIPELF
         end-$(CONFIG_GEMINI)		:= gemini
   tftpimage-$(CONFIG_GEMINI)		:= /tftpboot/zImage.$(end-y)

     extra.o-$(CONFIG_K2)		:= legacy.o
   tftpimage-$(CONFIG_K2)		:= /tftpboot/zImage.k2
   cacheflag-$(CONFIG_K2)		:= -include $(clear_L2_L3)

# kconfig 'feature', only one of these will ever be 'y' at a time.
# The rest will be unset.
motorola := $(CONFIG_MCPN765)$(CONFIG_MVME5100)$(CONFIG_PRPMC750) \
$(CONFIG_PRPMC800)$(CONFIG_LOPEC)$(CONFIG_PPLUS)
pcore := $(CONFIG_PCORE)$(CONFIG_POWERPMC250)

      zimage-$(motorola)		:= zImage-PPLUS
zimageinitrd-$(motorola)		:= zImage.initrd-PPLUS
   tftpimage-$(motorola)		:= /tftpboot/zImage.pplus
    znetboot-$(motorola)		:= zImage.pplus
  znetbootrd-$(motorola)		:= zImage.initrd.pplus

# Overrides previous assingment
     extra.o-$(CONFIG_PPLUS)		:= legacy.o

      zimage-$(pcore)			:= zImage-STRIPELF
zimageinitrd-$(pcore)			:= zImage.initrd-STRIPELF
     extra.o-$(pcore)			:= chrpmap.o
         end-$(pcore)			:= pcore
   tftpimage-$(pcore)			:= /tftpboot/zImage.$(end-y)
   cacheflag-$(pcore)			:= -include $(clear_L2_L3)

   tftpimage-$(CONFIG_SANDPOINT)	:= /tftpboot/zImage.sandpoint
   cacheflag-$(CONFIG_SANDPOINT)	:= -include $(clear_L2_L3)

      zimage-$(CONFIG_SPRUCE)		:= zImage-TREE
zimageinitrd-$(CONFIG_SPRUCE)		:= zImage.initrd-TREE
         end-$(CONFIG_SPRUCE)		:= spruce
  entrypoint-$(CONFIG_SPRUCE)		:= 0x00800000
        misc-$(CONFIG_SPRUCE)		:= misc-spruce.o
   tftpimage-$(CONFIG_SPRUCE)		:= /tftpboot/zImage.$(end-y)


# tftp image is prefixed with .smp if compiled for SMP
tftpimage-$(CONFIG_SMP)	+= .smp

# This is a treeboot that needs init functions until the
# boot rom is sorted out (i.e. this is short lived)
extra-aflags-$(CONFIG_REDWOOD_4)	:= -Wa,-m405
extra.o-$(CONFIG_REDWOOD_4)		:= rw4/rw4_init.o rw4/rw4_init_brd.o
EXTRA_AFLAGS := $(extra-aflags-y)
# head.o needs to get the cacheflags defined.
AFLAGS_head.o				+= $(cacheflag-y)

# Linker args.  This specifies where the image will be run at.
LD_ARGS				:= -T $(boot)/ld.script \
				   -Ttext $(CONFIG_BOOT_LOAD) -Bstatic
OBJCOPY_ARGS			:= -O elf32-powerpc

# head.o and relocate.o must be at the start.
boot-y				:= head.o relocate.o $(extra.o-y) $(misc-y)
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
zImage: $(images)/$(zimage-y) $(obj)/zvmlinux
	cp -f $(obj)/zvmlinux $(images)/zImage.elf
	rm -f $(obj)/zvmlinux

zImage.initrd: $(images)/$(zimageinitrd-y) $(obj)/zvmlinux.initrd
	cp -f $(obj)/zvmlinux.initrd $(images)/zImage.initrd.elf
	rm -f $(obj)/zvmlinux.initrd

znetboot: zImage
ifneq ($(ZNETBOOT),)
	cp $(images)/$(ZNETBOOT) $(tftpimage-y)
else
	cp $(images)/zImage.* $(tftpimage-y)
endif

znetboot.initrd: zImage.initrd
ifneq ($(znetbootrd-y),)
	cp $(images)/$(znetbootrd-y) $(tftpimage-y)
else
	cp $(images)/zImage.* $(tftpimage-y)
endif

$(images)/zImage-STRIPELF: $(obj)/zvmlinux
	dd if=$(obj)/zvmlinux of=$(images)/zImage.$(end-y) skip=64 bs=1k

$(images)/zImage.initrd-STRIPELF: $(obj)/zvmlinux.initrd
	dd if=$(obj)/zvmlinux.initrd of=$(images)/zImage.initrd.$(end-y) \
		skip=64 bs=1k

$(images)/zImage-TREE: $(obj)/zvmlinux $(MKTREE)
	$(MKTREE) $(obj)/zvmlinux $(images)/zImage.$(end-y) $(ENTRYPOINT)

$(images)/zImage.initrd-TREE: $(obj)/zvmlinux.initrd $(MKTREE)
	$(MKTREE) $(obj)/zvmlinux.initrd $(images)/zImage.initrd.$(end-y) \
		$(ENTRYPOINT)

$(images)/zImage-PPLUS: $(obj)/zvmlinux $(MKPREP) $(MKBUGBOOT)
	$(MKPREP) -pbp $(obj)/zvmlinux $(images)/zImage.pplus
	$(MKBUGBOOT) $(obj)/zvmlinux $(images)/zImage.bugboot

$(images)/zImage.initrd-PPLUS: $(obj)/zvmlinux.initrd $(MKPREP) $(MKBUGBOOT)
	$(MKPREP) -pbp $(obj)/zvmlinux.initrd $(images)/zImage.initrd.pplus
	$(MKBUGBOOT) $(obj)/zvmlinux.initrd $(images)/zImage.initrd.bugboot
