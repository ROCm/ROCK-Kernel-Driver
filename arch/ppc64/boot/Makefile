# Makefile for making ELF bootable images for booting on CHRP
# using Open Firmware.
#
# Geert Uytterhoeven	September 1997
#
# Based on coffboot by Paul Mackerras
# Simplified for ppc64 by Todd Inglett
#
# NOTE:	this code is built for 32 bit in ELF32 format even though
#	it packages a 64 bit kernel.  We do this to simplify the
#	bootloader and increase compatibility with OpenFirmware.
#
#	To this end we need to define BOOTCC, etc, as the tools
#	needed to build the 32 bit image.  These are normally HOSTCC,
#	but may be a third compiler if, for example, you are cross
#	compiling from an intel box.  Once the 64bit ppc gcc is
#	stable it will probably simply be a compiler switch to
#	compile for 32bit mode.
#	To make it easier to setup a cross compiler,
#	CROSS32_COMPILE is setup as a prefix just like CROSS_COMPILE
#	in the toplevel makefile.

CROSS32_COMPILE =
#CROSS32_COMPILE = /usr/local/ppc/bin/powerpc-linux-

BOOTCC = $(CROSS32_COMPILE)gcc
BOOTCFLAGS = $(HOSTCFLAGS) -I$(HPATH)
BOOTLD = $(CROSS32_COMPILE)ld
BOOTAS = $(CROSS32_COMPILE)as
BOOTAFLAGS = -D__ASSEMBLY__ $(HOSTCFLAGS)

.c.o:
	$(BOOTCC) $(BOOTCFLAGS) -c -o $*.o $<
.S.o:
	$(BOOTCC) $(BOOTAFLAGS) -traditional -c -o $*.o $<

CFLAGS	= $(CPPFLAGS) -O -fno-builtin -DSTDC_HEADERS
LD_ARGS = -Ttext 0x00400000 -e _start
OBJCOPYFLAGS := -S -O binary

OBJS = crt0.o start.o main.o zlib.o image.o imagesize.o
#LIBS = $(TOPDIR)/lib/lib.a
LIBS =

ifeq ($(CONFIG_SMP),y)
TFTPIMAGE=/tftpboot/zImage.chrp.smp
else
TFTPIMAGE=/tftpboot/zImage.chrp
endif


ifeq ($(CONFIG_PPC_ISERIES),y)
all:	vmlinux.sm
else
all:	$(TOPDIR)/zImage
endif


znetboot: zImage
	cp zImage $(TFTPIMAGE)


ifeq ($(CONFIG_PPC_ISERIES),y)

addSystemMap: addSystemMap.c
	$(HOSTCC) $(HOSTCFLAGS) -o addSystemMap addSystemMap.c

vmlinux.sm: $(TOPDIR)/vmlinux addSystemMap
	./addSystemMap $(TOPDIR)/System.map $(TOPDIR)/vmlinux vmlinux.sm


addRamDisk: addRamDisk.c
	$(HOSTCC) $(HOSTCFLAGS) -o addRamDisk addRamDisk.c

vmlinux.initrd: $(TOPDIR)/vmlinux addRamDisk ramdisk.image.gz $(TOPDIR)/System.map
	./addRamDisk ramdisk.image.gz $(TOPDIR)/System.map $(TOPDIR)/vmlinux vmlinux.initrd

vmlinux.sminitrd: vmlinux.sm addRamDisk ramdisk.image.gz $(TOPDIR)/System.map
	./addRamDisk ramdisk.image.gz $(TOPDIR)/System.map vmlinux.sm vmlinux.sminitrd

endif


znetboot.initrd: zImage.initrd
	cp zImage.initrd $(TFTPIMAGE)

floppy: zImage
	mcopy zImage a:zImage

piggyback: piggyback.c
	$(HOSTCC) $(HOSTCFLAGS) -DKERNELBASE=$(KERNELBASE) -o piggyback piggyback.c

addnote: addnote.c
	$(HOSTCC) $(HOSTCFLAGS) -o addnote addnote.c

image.o: piggyback vmlinux.gz
	./piggyback image < vmlinux.gz | $(BOOTAS) -o image.o

sysmap.o: piggyback ../../../System.map
	./piggyback sysmap < ../../../System.map | $(BOOTAS) -o sysmap.o

initrd.o: ramdisk.image.gz piggyback
	./piggyback initrd < ramdisk.image.gz | $(BOOTAS) -o initrd.o

zImage: $(OBJS) no_initrd.o addnote
	$(BOOTLD) $(LD_ARGS) -T zImage.lds -o $@ $(OBJS) no_initrd.o $(LIBS)
	./addnote $@

zImage.initrd: $(OBJS) initrd.o addnote
	$(BOOTLD) $(LD_ARGS) -T zImage.lds -o $@ $(OBJS) initrd.o $(LIBS)
	./addnote $@

vmlinux.gz: $(TOPDIR)/vmlinux
	$(OBJCOPY) $(OBJCOPYFLAGS) $(TOPDIR)/vmlinux vmlinux
	ls -l vmlinux | awk '{printf "/* generated -- do not edit! */\nint uncompressed_size = %d;\n", $$5}' > imagesize.c
	$(CROSS_COMPILE)nm -n $(TOPDIR)/vmlinux | tail -1 | awk '{printf "long vmlinux_end = 0x%s;\n", substr($$1,8)}' >> imagesize.c
	gzip -vf9 vmlinux

imagesize.c: vmlinux.gz

clean:
	rm -f piggyback note addnote $(OBJS) zImage zImage.initrd vmlinux.gz no_initrd.o imagesize.c addSystemMap vmlinux.sm addRamDisk vmlinux.initrd vmlinux.sminitrd 

dep:
	$(CPP) $(CPPFLAGS) -M *.S *.c > .depend

