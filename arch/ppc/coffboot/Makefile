# Makefile for making XCOFF bootable images for booting on PowerMacs
# using Open Firmware.
#
# Paul Mackerras	January 1997

HOSTCFLAGS = -O -I$(TOPDIR)/include

CFLAGS	= $(CPPFLAGS) -O -fno-builtin
OBJCOPY_ARGS = -O aixcoff-rs6000 -R .stab -R .stabstr -R .comment
COFF_LD_ARGS = -e _start -T ld.script -Ttext 500000 -Tdata 510000 -Bstatic
CHRP_LD_ARGS = -Ttext 0x01000000

COFFOBJS = coffcrt0.o start.o coffmain.o misc.o string.o zlib.o image.o
CHRPOBJS = crt0.o start.o chrpmain.o misc.o string.o zlib.o image.o
LIBS = $(TOPDIR)/lib/lib.a

ifeq ($(CONFIG_PPC64BRIDGE),y)
MSIZE=.64
else
MSIZE=
endif

ifeq ($(CONFIG_ALL_PPC),y)
# yes, we want to build pmac stuff
CONFIG_PMAC = y
endif

ifeq ($(CONFIG_SMP),y)
TFTPIMAGE=/tftpboot/zImage.pmac.smp$(MSIZE)
else
TFTPIMAGE=/tftpboot/zImage.pmac$(MSIZE)
endif

ifeq ($(CONFIG_PMAC),y)
chrpmain.o: chrpmain.c
	$(CC) $(CFLAGS) -DSYSMAP_OFFSET=0 -DSYSMAP_SIZE=0 -c chrpmain.c

hack-coff: hack-coff.c
	$(HOSTCC) $(HOSTCFLAGS) -o hack-coff hack-coff.c

znetboot: vmlinux.coff vmlinux.elf zImage
	cp vmlinux.coff $(TFTPIMAGE)
	cp vmlinux.elf $(TFTPIMAGE).elf

znetboot.initrd: vmlinux.coff.initrd vmlinux.elf.initrd
	cp vmlinux.coff.initrd $(TFTPIMAGE)
	cp vmlinux.elf.initrd $(TFTPIMAGE).elf

floppy: zImage
#	mount -t hfs /dev/fd0 /mnt
#	cp vmlinux.coff /mnt
#	umount /mnt

miboot.image: dummy.o vmlinux.gz
	$(OBJCOPY) $(OBJCOPY_ARGS) --add-section=image=vmlinux.gz dummy.o $@

miboot.image.initrd: miboot.image ramdisk.image.gz
	$(OBJCOPY) $(OBJCOPY_ARGS) --add-section=initrd=ramdisk.image.gz miboot.image $@

coffboot: $(COFFOBJS) no_initrd.o ld.script
	$(LD) -o $@ $(COFF_LD_ARGS) $(COFFOBJS) no_initrd.o $(LIBS)

coffboot.initrd: $(COFFOBJS) initrd.o ld.script
	$(LD) -o $@ $(COFF_LD_ARGS) $(COFFOBJS) initrd.o $(LIBS)

piggyback: piggyback.c
	$(HOSTCC) $(HOSTCFLAGS) -DKERNELBASE=$(KERNELBASE) -o piggyback piggyback.c

mknote: mknote.c
	$(HOSTCC) $(HOSTCFLAGS) -o mknote mknote.c

image.o: piggyback vmlinux.gz
	./piggyback image < vmlinux.gz | $(AS) -o image.o

initrd.o: ramdisk.image.gz piggyback
	./piggyback initrd < ramdisk.image.gz | $(AS) -o initrd.o

vmlinux.coff: coffboot hack-coff
	$(OBJCOPY) $(OBJCOPY_ARGS) coffboot $@
	./hack-coff $@
	ln -sf vmlinux.coff zImage

vmlinux.coff.initrd: coffboot.initrd hack-coff
	$(OBJCOPY) $(OBJCOPY_ARGS) coffboot.initrd $@
	./hack-coff $@

vmlinux.elf: $(CHRPOBJS) no_initrd.o mknote
	$(LD) $(CHRP_LD_ARGS) -o $@ $(CHRPOBJS) no_initrd.o $(LIBS)
	./mknote > note
	$(OBJCOPY) $@ $@ --add-section=.note=note \
		--add-section=sysmap=../../../System.map -R .comment
	$(CC) $(CFLAGS) chrpmain.c -c -o chrpmain.o \
		-DSYSMAP_OFFSET=`sh ../boot/offset $(OBJDUMP) $@ sysmap` \
		-DSYSMAP_SIZE=`sh ../boot/size $(OBJDUMP) $@ sysmap`
	$(LD) $(CHRP_LD_ARGS) -o $@ $(CHRPOBJS) no_initrd.o $(LIBS)
	$(OBJCOPY) $@ $@ --add-section=.note=note \
		--add-section=sysmap=../../../System.map -R .comment

vmlinux.elf.initrd: $(CHRPOBJS) initrd.o mknote
	$(LD) $(CHRP_LD_ARGS) -o $@ $(CHRPOBJS) initrd.o $(LIBS)
	./mknote > note
	$(OBJCOPY) $@ $@ --add-section=.note=note -R .comment

zImage: vmlinux.coff vmlinux.elf miboot.image

zImage.initrd: vmlinux.coff.initrd vmlinux.elf.initrd miboot.image.initrd

else
znetboot: vmlinux.gz

znetboot.initrd: vmlinux.gz

coffboot: vmlinux.gz

zImage: vmlinux.gz

zImage.initrd: vmlinux.gz

vmlinux.coff: vmlinux.gz

vmlinux.coff.initrd: vmlinux.gz

floppy: vmlinux.gz

endif

vmlinux.gz: $(TOPDIR)/vmlinux
	$(OBJCOPY) -S -O binary $(TOPDIR)/vmlinux vmlinux
	gzip -vf9 vmlinux

clean:
	rm -f hack-coff coffboot zImage vmlinux.coff vmlinux.gz
	rm -f mknote piggyback vmlinux.elf note
	rm -f miboot.image miboot.image.initrd

fastdep:
