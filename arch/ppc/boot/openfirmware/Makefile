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

OBJCOPY_ARGS = -O aixcoff-rs6000 -R .stab -R .stabstr -R .comment
COFF_LD_ARGS = -T ../ld.script -e _start -Ttext 0x00500000 -Bstatic
CHRP_LD_ARGS = -T ../ld.script -e _start -Ttext 0x00400000
NEWWORLD_LD_ARGS = -T ../ld.script -e _start -Ttext 0x01000000

COMMONOBJS = start.o misc.o ../common/string.o common.o
COFFOBJS = ../common/coffcrt0.o $(COMMONOBJS) coffmain.o
CHRPOBJS = ../common/crt0.o $(COMMONOBJS) chrpmain.o
NEWWORLDOBJS = ../common/crt0.o $(COMMONOBJS) newworldmain.o

EXTRA_TARGETS	:= $(COFFOBJS) $(CHRPOBJS) $(NEWWORLDOBJS)
LIBS = $(TOPDIR)/lib/lib.a ../lib/lib.a ../of1275/of1275.a

ADDNOTE := ../utils/addnote
MKNOTE := ../utils/mknote
SIZE := ../utils/size
OFFSET := ../utils/offset
HACKCOFF := ../utils/hack-coff

ifdef CONFIG_SMP
END := .smp
endif
ifdef CONFIG_PPC64BRIDGE
END += .64
endif

TFTPIMAGE=/tftpboot/zImage.

../common/coffcrt0.o:
	$(MAKE) -C ../common coffcrt0.o

image.o: ../images/vmlinux.gz ../common/dummy.o
	$(OBJCOPY) ../common/dummy.o $@ -R .comment \
		--add-section=.image=../images/vmlinux.gz \
		--set-section-flags=.image=contents,alloc,load,readonly,data
ifdef CONFIG_XMON
	$(OBJCOPY) $@ $@ \
		--add-section=.sysmap=$(TOPDIR)/System.map \
		--set-section-flags=.sysmap=contents,alloc,load,readonly,data
endif

# Place the ramdisk in the initrd image.
image-initrd.o: image.o ../images/ramdisk.image.gz
	$(OBJCOPY) image.o $@ \
		--add-section=.ramdisk=../images/ramdisk.image.gz \
		--set-section-flags=.ramdisk=contents,alloc,load,readonly,data

# Create the note section for New-World PowerMacs.
note: $(MKNOTE)
	$(MKNOTE) > note

znetboot: vmlinux.coff vmlinux.elf-pmac zImage
	cp ../images/vmlinux.coff $(TFTPIMAGE).pmac$(END)
	cp ../images/vmlinux.elf-pmac $(TFTPIMAGE).pmac$(END)elf
	cp ../images/zImage.chrp $(TFTPIMAGE).chrp$(END)

znetboot.initrd: vmlinux.initrd.coff vmlinux.initrd.elf-pmac
	cp ../images/vmlinux.initrd.coff $(TFTPIMAGE).pmac$(END)
	cp ../images/vmlinux.initrd.elf-pmac $(TFTPIMAGE).pmac$(END).elf
	cp ../images/zImage.initrd.chrp $(TFTPIMAGE).chrp$(END)

miboot.image: ../common/dummy.o ../images/vmlinux.gz
	$(OBJCOPY) $(OBJCOPY_ARGS) --add-section=image=../images/vmlinux.gz \
		../common/dummy.o ../images/$@

miboot.initrd.image: miboot.image ../images/ramdisk.image.gz
	$(OBJCOPY) $(OBJCOPY_ARGS) \
		--add-section=initrd=../images/ramdisk.image.gz \
		../images/miboot.image ../images/$@

coffboot: $(COFFOBJS) image.o $(LIBS)
	$(LD) -o $@ $(COFF_LD_ARGS) $^
	$(OBJCOPY) $@ $@ -R .comment -R .ramdisk

coffboot.initrd: $(COFFOBJS) image-initrd.o $(LIBS)
	$(LD) -o $@ $(COFF_LD_ARGS) $^
	$(OBJCOPY) $@ $@ -R .comment

vmlinux.coff: coffboot $(HACKCOFF)
	$(OBJCOPY) $(OBJCOPY_ARGS) coffboot ../images/$@
	$(HACKCOFF) ../images/$@
	rm -f coffboot
	ln -sf vmlinux.coff ../images/zImage.pmac

vmlinux.initrd.coff: coffboot.initrd $(HACKCOFF)
	$(OBJCOPY) $(OBJCOPY_ARGS) coffboot.initrd ../images/$@
	$(HACKCOFF) ../images/$@
	rm -f coffboot.initrd
	ln -sf vmlinux.initrd.coff ../images/zImage.initrd.pmac

vmlinux.elf-pmac: $(NEWWORLDOBJS) $(LIBS) image.o
	$(LD) $(NEWWORLD_LD_ARGS) -o ../images/$@ $^

vmlinux.initrd.elf-pmac: $(NEWWORLDOBJS) $(LIBS) image-initrd.o
	$(LD) $(NEWWORLD_LD_ARGS) -o ../images/$@ $^

zImage.chrp: $(CHRPOBJS) image.o $(LIBS)
	$(LD) $(CHRP_LD_ARGS) -o ../images/$@ $^

zImage.initrd.chrp: $(CHRPOBJS) image-initrd.o $(LIBS)
	$(LD) $(CHRP_LD_ARGS) -o ../images/$@ $^

zImage: vmlinux.coff vmlinux.elf-pmac zImage.chrp miboot.image $(ADDNOTE) \
	note
	$(OBJCOPY) ../images/vmlinux.elf-pmac ../images/vmlinux.elf-pmac \
		--add-section=.note=note -R .comment -R .ramdisk
	$(OBJCOPY) ../images/zImage.chrp ../images/zImage.chrp \
		-R .comment -R .ramdisk
	cp ../images/zImage.chrp ../images/zImage.chrp-rs6k
	$(ADDNOTE) ../images/zImage.chrp-rs6k

zImage.initrd: vmlinux.initrd.coff vmlinux.initrd.elf-pmac zImage.initrd.chrp \
	miboot.initrd.image $(ADDNOTE) note
	$(OBJCOPY) ../images/vmlinux.initrd.elf-pmac \
		../images/vmlinux.initrd.elf-pmac --add-section=.note=note \
		-R .comment
	$(OBJCOPY) ../images/zImage.initrd.chrp ../images/zImage.initrd.chrp \
		-R .comment
	cp ../images/zImage.initrd.chrp ../images/zImage.initrd.chrp-rs6k
	$(ADDNOTE) ../images/zImage.initrd.chrp-rs6k

clean:
	rm -f note

include $(TOPDIR)/Rules.make
