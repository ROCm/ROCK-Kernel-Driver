# BK Id: SCCS/s.Makefile 1.14 07/27/01 20:24:17 trini
#
# Makefile for making XCOFF bootable images for booting on PowerMacs
# using Open Firmware.
#
# Paul Mackerras	January 1997
#
# Cleaned up, moved into arch/ppc/boot/pmac
# Tom Rini		January 2001

OBJCOPY_ARGS = -O aixcoff-rs6000 -R .stab -R .stabstr -R .comment
COFF_LD_ARGS = -e _start -T ld.script -Ttext 500000 -Tdata 510000 -Bstatic
CHRP_LD_ARGS = -Ttext 0x01000000

COMMONOBJS = start.o misc.o ../common/string.o image.o ../common/ofcommon.o
COFFOBJS = ../common/coffcrt0.o $(COMMONOBJS) coffmain.o
CHRPOBJS = ../common/crt0.o $(COMMONOBJS) chrpmain.o
LIBS = $(TOPDIR)/lib/lib.a ../lib/zlib.a

MKNOTE := ../utils/mknote
SIZE := ../utils/size
OFFSET := ../utils/offset
PIGGYBACK := ../utils/piggyback
HACKCOFF := ../utils/hack-coff

ifeq ($(CONFIG_PPC64BRIDGE),y)
MSIZE=.64
else
MSIZE=
endif

ifeq ($(CONFIG_SMP),y)
TFTPIMAGE=/tftpboot/zImage.pmac.smp$(MSIZE)
else
TFTPIMAGE=/tftpboot/zImage.pmac$(MSIZE)
endif

../common/crt0.o:
	$(MAKE) -C ../common crt0.o

../common/coffcrt0.o:
	$(MAKE) -C ../common coffcrt0.o

chrpmain.o: chrpmain.c
	$(CC) $(CFLAGS) -DSYSMAP_OFFSET=0 -DSYSMAP_SIZE=0 -c chrpmain.c

znetboot: vmlinux.coff vmlinux.elf-pmac zImage
	cp ../images/vmlinux.coff $(TFTPIMAGE)
	cp ../images/vmlinux.elf-pmac $(TFTPIMAGE).elf

znetboot.initrd: vmlinux.coff.initrd vmlinux.initrd.elf-pmac
	cp ../images/vmlinux.coff.initrd $(TFTPIMAGE)
	cp ../images/vmlinux.elf-pmac.initrd $(TFTPIMAGE).elf

#floppy: zImage
#	mount -t hfs /dev/fd0 /mnt
#	cp vmlinux.coff /mnt
#	umount /mnt

miboot.image: dummy.o ../images/vmlinux.gz
	$(OBJCOPY) $(OBJCOPY_ARGS) --add-section=image=../images/vmlinux.gz \
		dummy.o ../images/$@

miboot.image.initrd: miboot.image ../images/ramdisk.image.gz
	$(OBJCOPY) $(OBJCOPY_ARGS) --add-section=initrd=../images/ramdisk.image.gz \
		../images/miboot.image ../images/$@

coffboot: $(COFFOBJS) $(LIBS) ../common/no_initrd.o ld.script ../images/vmlinux.gz
	$(LD) -o $@ $(COFF_LD_ARGS) $(COFFOBJS) ../common/no_initrd.o $(LIBS)

coffboot.initrd: $(COFFOBJS) $(LIBS) initrd.o ld.script ../images/vmlinux.gz
	$(LD) -o $@ $(COFF_LD_ARGS) $(COFFOBJS) initrd.o $(LIBS)

image.o: $(PIGGYBACK) ../images/vmlinux.gz
	$(PIGGYBACK) image < ../images/vmlinux.gz | $(AS) -o $@

initrd.o: ../images/ramdisk.image.gz $(PIGGYBACK)
	$(PIGGYBACK) initrd < ../images/ramdisk.image.gz | $(AS) -o $@

vmlinux.coff: coffboot $(HACKCOFF)
	$(OBJCOPY) $(OBJCOPY_ARGS) coffboot ../images/$@
	$(HACKCOFF) ../images/$@
	rm -f coffboot
	ln -sf vmlinux.coff ../images/zImage.pmac

vmlinux.coff.initrd: coffboot.initrd $(HACKCOFF)
	$(OBJCOPY) $(OBJCOPY_ARGS) coffboot.initrd ../images/$@
	$(HACKCOFF) ../images/$@
	rm -f coffboot.initrd
	ln -sf vmlinux.coff.initrd ../images/zImage.initrd.pmac

vmlinux.elf-pmac: $(CHRPOBJS) $(LIBS) ../common/no_initrd.o $(MKNOTE) ../images/vmlinux.gz
	$(LD) $(CHRP_LD_ARGS) -o ../images/$@ $(CHRPOBJS) ../common/no_initrd.o $(LIBS)
	$(MKNOTE) > note
	$(OBJCOPY) ../images/$@ ../images/$@ --add-section=.note=note \
		--add-section=sysmap=$(TOPDIR)/System.map -R .comment
	$(CC) $(CFLAGS) chrpmain.c -c -o chrpmain.o \
		-DSYSMAP_OFFSET=`sh $(OFFSET) $(OBJDUMP) ../images/$@ sysmap` \
		-DSYSMAP_SIZE=`sh $(SIZE) $(OBJDUMP) ../images/$@ sysmap`
	$(LD) $(CHRP_LD_ARGS) -o ../images/$@ $(CHRPOBJS) ../common/no_initrd.o $(LIBS)
	$(OBJCOPY) ../images/$@ ../images/$@ --add-section=.note=note \
		--add-section=sysmap=$(TOPDIR)/System.map -R .comment
	rm -f note

vmlinux.initrd.elf-pmac: $(CHRPOBJS) $(LIBS) initrd.o $(MKNOTE) ../images/vmlinux.gz
	$(LD) $(CHRP_LD_ARGS) -o ../images/$@ $(CHRPOBJS) initrd.o $(LIBS)
	$(MKNOTE) > note
	$(OBJCOPY) ../images/$@ ../images/$@ --add-section=.note=note -R .comment
	rm -f note

zImage: vmlinux.coff vmlinux.elf-pmac miboot.image

zImage.initrd: vmlinux.coff.initrd vmlinux.initrd.elf-pmac miboot.image.initrd

include $(TOPDIR)/Rules.make
