# Makefile for making bootable images on various OpenFirmware machines.
#
# Paul Mackerras	January 1997
#	XCOFF bootable images for PowerMacs
# Geert Uytterhoeven	September 1997
#	ELF bootable iamges for CHRP machines.
# Tom Rini		January 2001
# 	Cleaned up, moved into arch/ppc/boot/pmac
# Tom Rini		July/August 2002
#	Merged 'chrp' and 'pmac' into 'openfirmware', and cleaned up the
#	rules.

clean-files := note coffboot coffboot.initrd

boot: zImage

boot := arch/ppc/boot
common := $(boot)/common
utils := $(boot)/utils
bootlib := $(boot)/lib
of1275 := $(boot)/of1275
images := $(boot)/images

OBJCOPY_ARGS = -O aixcoff-rs6000 -R .stab -R .stabstr -R .comment
COFF_LD_ARGS = -T $(boot)/ld.script -e _start -Ttext 0x00700000 -Bstatic
CHRP_LD_ARGS = -T $(boot)/ld.script -e _start -Ttext 0x00800000
NEWWORLD_LD_ARGS = -T $(boot)/ld.script -e _start -Ttext 0x01000000

COMMONOBJS	:= start.o misc.o common.o
COFFOBJS	:= coffcrt0.o $(COMMONOBJS) coffmain.o
CHRPOBJS	:= crt0.o $(COMMONOBJS) chrpmain.o
NEWWORLDOBJS	:= crt0.o $(COMMONOBJS) newworldmain.o

EXTRA_TARGETS := $(COFFOBJS) $(CHRPOBJS) $(NEWWORLDOBJS)
COFFOBJS	:= $(addprefix $(obj)/, $(COFFOBJS))
CHRPOBJS	:= $(addprefix $(obj)/, $(CHRPOBJS))
NEWWORLDOBJS	:= $(addprefix $(obj)/, $(NEWWORLDOBJS))

LIBS = lib/lib.a $(bootlib)/lib.a $(of1275)/lib.a $(common)/lib.a

ADDNOTE := $(utils)/addnote
MKNOTE := $(utils)/mknote
HACKCOFF := $(utils)/hack-coff

ifdef CONFIG_SMP
END := .smp
endif
ifdef CONFIG_PPC64BRIDGE
END += .64
endif

TFTPIMAGE=/tftpboot/zImage.

$(obj)/dummy.o: $(common)/dummy.c
	$(CC) -c -o $@ $(common)/dummy.c

$(obj)/image.o: $(images)/vmlinux.gz $(obj)/dummy.o
	$(OBJCOPY) $(obj)/dummy.o $@ -R .comment \
		--add-section=.image=$(images)/vmlinux.gz \
		--set-section-flags=.image=contents,alloc,load,readonly,data
ifdef CONFIG_XMON
	$(OBJCOPY) $@ $@ \
		--add-section=.sysmap=$(TOPDIR)/System.map \
		--set-section-flags=.sysmap=contents,alloc,load,readonly,data
endif

# Place the ramdisk in the initrd image.
$(obj)/image-initrd.o: $(obj)/image.o $(images)/ramdisk.image.gz
	$(OBJCOPY) $(obj)/image.o $@ \
		--add-section=.ramdisk=$(images)/ramdisk.image.gz \
		--set-section-flags=.ramdisk=contents,alloc,load,readonly,data

# Create the note section for New-World PowerMacs.
$(obj)/note: $(MKNOTE)
	$(MKNOTE) > $(obj)/note

znetboot: $(images)/vmlinux.coff $(images)/vmlinux.elf-pmac $(images)/zImage.chrp
	cp $(images)/vmlinux.coff $(TFTPIMAGE).pmac$(END)
	cp $(images)/vmlinux.elf-pmac $(TFTPIMAGE).pmac$(END)elf
	cp $(images)/zImage.chrp $(TFTPIMAGE).chrp$(END)

znetboot.initrd: $(images)/vmlinux.initrd.coff \
		$(images)/vmlinux.initrd.elf-pmac \
		$(images)/zImage.initrd.chrp
	cp $(images)/vmlinux.initrd.coff $(TFTPIMAGE).pmac$(END)
	cp $(images)/vmlinux.initrd.elf-pmac $(TFTPIMAGE).pmac$(END).elf
	cp $(images)/zImage.initrd.chrp $(TFTPIMAGE).chrp$(END)

$(images)/miboot.image: $(obj)/dummy.o $(images)/vmlinux.gz
	$(OBJCOPY) $(OBJCOPY_ARGS) --add-section=image=$(images)/vmlinux.gz \
		$(obj)/dummy.o $@

$(images)/miboot.initrd.image: $(images)/miboot.image $(images)/ramdisk.image.gz
	$(OBJCOPY) $(OBJCOPY_ARGS) \
		--add-section=initrd=$(images)/ramdisk.image.gz \
		$(images)/miboot.image $@

$(obj)/coffcrt0.o: $(common)/crt0.S
	$(CC) $(AFLAGS) -DXCOFF -traditional -c -o $@ $(common)/crt0.S

$(obj)/crt0.o: $(common)/crt0.S
	$(CC) $(AFLAGS) -traditional -c -o $@ $(common)/crt0.S

$(obj)/coffboot: $(COFFOBJS) $(obj)/image.o $(LIBS)
	$(LD) -o $@ $(COFF_LD_ARGS) $^
	$(OBJCOPY) $@ $@ -R .comment -R .ramdisk

$(obj)/coffboot.initrd: $(COFFOBJS) $(obj)/image-initrd.o $(LIBS)
	$(LD) -o $@ $(COFF_LD_ARGS) $^
	$(OBJCOPY) $@ $@ -R .comment

$(images)/vmlinux.coff: $(obj)/coffboot $(HACKCOFF)
	$(OBJCOPY) $(OBJCOPY_ARGS) $(obj)/coffboot $@
	$(HACKCOFF) $@
	ln -sf vmlinux.coff $(images)/zImage.pmac

$(images)/vmlinux.initrd.coff: $(obj)/coffboot.initrd $(HACKCOFF)
	$(OBJCOPY) $(OBJCOPY_ARGS) $(obj)/coffboot.initrd $@
	$(HACKCOFF) $@
	ln -sf vmlinux.initrd.coff $(images)/zImage.initrd.pmac

$(images)/vmlinux.elf-pmac: $(NEWWORLDOBJS) $(LIBS) $(obj)/image.o $(obj)/note
	$(LD) $(NEWWORLD_LD_ARGS) -o $@ $(NEWWORLDOBJS) $(LIBS) $(obj)/image.o
	$(OBJCOPY) $@ $@ \
		--add-section=.note=$(obj)/note -R .comment -R .ramdisk

$(images)/vmlinux.initrd.elf-pmac: $(NEWWORLDOBJS) $(LIBS) \
		$(obj)/image-initrd.o $(obj)/note
	$(LD) $(NEWWORLD_LD_ARGS) -o $@ $(NEWWORLDOBJS) $(LIBS) \
		$(obj)/image-initrd.o
	$(OBJCOPY) $@ $@ \
		--add-section=.note=$(obj)/note -R .comment

$(images)/zImage.chrp: $(CHRPOBJS) $(obj)/image.o $(LIBS)
	$(LD) $(CHRP_LD_ARGS) -o $@ $^
	$(OBJCOPY) $@ $@ -R .comment -R .ramdisk

$(images)/zImage.initrd.chrp: $(CHRPOBJS) $(obj)/image-initrd.o $(LIBS)
	$(LD) $(CHRP_LD_ARGS) -o $@ $^
	$(OBJCOPY) $@ $@ -R .comment

$(images)/zImage.chrp-rs6k: $(images)/zImage.chrp $(ADDNOTE)
	cp $(images)/zImage.chrp $@
	$(ADDNOTE) $@

$(images)/zImage.initrd.chrp-rs6k: $(images)/zImage.initrd.chrp $(ADDNOTE)
	cp $(images)/zImage.initrd.chrp $@
	$(ADDNOTE) $@

zImage: $(images)/vmlinux.coff $(images)/vmlinux.elf-pmac \
		$(images)/zImage.chrp $(images)/zImage.chrp-rs6k \
		$(images)/miboot.image

zImage.initrd: $(images)/vmlinux.initrd.coff $(images)/vmlinux.initrd.elf-pmac\
		$(images)/zImage.initrd.chrp $(images)/zImage.initrd.chrp-rs6k\
		$(images)/miboot.initrd.image
