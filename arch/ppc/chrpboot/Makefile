# Makefile for making ELF bootable images for booting on CHRP
# using Open Firmware.
#
# Geert Uytterhoeven	September 1997
#
# Based on coffboot by Paul Mackerras

ifeq ($(CONFIG_PPC64BRIDGE),y)
MSIZE=.64
AFLAGS += -Wa,-mppc64bridge
else
MSIZE=
endif

.c.o:
	$(CC) $(CFLAGS) -DKERNELBASE=$(KERNELBASE) -c -o $*.o $<
.S.o:
	$(CC) $(AFLAGS) -traditional -c -o $*.o $<

CFLAGS	= $(CPPFLAGS) -O -fno-builtin -DSTDC_HEADERS
LD_ARGS = -Ttext 0x00400000

OBJS = crt0.o start.o main.o misc.o ../coffboot/string.o ../coffboot/zlib.o image.o
LIBS = $(TOPDIR)/lib/lib.a

ifeq ($(CONFIG_ALL_PPC),y)
# yes, we want to build chrp stuff
CONFIG_CHRP = y
endif

ifeq ($(CONFIG_SMP),y)
TFTPIMAGE=/tftpboot/zImage.chrp.smp$(MSIZE)
else
TFTPIMAGE=/tftpboot/zImage.chrp$(MSIZE)
endif

all:	$(TOPDIR)/zImage

#
# Only build anything here if we're configured for CHRP
# -- cort
#
ifeq ($(CONFIG_CHRP),y)
znetboot: zImage
	cp zImage $(TFTPIMAGE)

znetboot.initrd: zImage.initrd
	cp zImage.initrd $(TFTPIMAGE)

floppy: zImage
	mcopy zImage a:zImage

piggyback: piggyback.c
	$(HOSTCC) $(HOSTCFLAGS) -DKERNELBASE=$(KERNELBASE) -o piggyback piggyback.c

addnote: addnote.c
	$(HOSTCC) $(HOSTCFLAGS) -o addnote addnote.c

image.o: piggyback ../coffboot/vmlinux.gz
	./piggyback image < ../coffboot/vmlinux.gz | $(AS) -o image.o

sysmap.o: piggyback ../../../System.map
	./piggyback sysmap < ../../../System.map | $(AS) -o sysmap.o

initrd.o: ramdisk.image.gz piggyback
	./piggyback initrd < ramdisk.image.gz | $(AS) -o initrd.o

zImage: $(OBJS) no_initrd.o addnote
	$(LD) $(LD_ARGS) -o $@ $(OBJS) no_initrd.o $(LIBS)
	cp $@ $@.rs6k
	./addnote $@.rs6k

zImage.initrd: $(OBJS) initrd.o addnote
	$(LD) $(LD_ARGS) -o $@ $(OBJS) initrd.o $(LIBS)
	cp $@ $@.rs6k
	./addnote $@.rs6k

else
znetboot:

znetboot.initrd:

floppy:

zImage:

zImage.initrd:

endif

# just here to match coffboot/Makefile
vmlinux.coff:

vmlinux.coff.initrd:


clean:
	rm -f piggyback note addnote $(OBJS) zImage

fastdep:
	$(TOPDIR)/scripts/mkdep *.[Sch] > .depend

dep:
	$(CPP) $(CPPFLAGS) -M *.S *.c > .depend

