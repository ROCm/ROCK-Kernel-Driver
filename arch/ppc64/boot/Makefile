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
BOOTCFLAGS = $(HOSTCFLAGS) -I$(objtree)/include
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

OBJS = $(addprefix $(obj)/,crt0.o start.o main.o zlib.o image.o imagesize.o)

ifeq ($(CONFIG_PPC_ISERIES),y)

boot:	vmlinux.sm

$(obj)/addSystemMap: $(obj)/addSystemMap.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

$(obj)/vmlinux.sm: vmlinux $(obj)/addSystemMap
	$(obj)/addSystemMap System.map vmlinux $(obj)/vmlinux.sm

$(obj)/addRamDisk: $(obj)/addRamDisk.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

$(obj)/vmlinux.initrd: vmlinux $(obj)/addRamDisk $(obj)/ramdisk.image.gz System.map
	$(obj)/addRamDisk $(obj)/ramdisk.image.gz System.map vmlinux $(obj)/vmlinux.initrd

$(obj)/vmlinux.sminitrd: $(obj)/vmlinux.sm $(obj)/addRamDisk $(obj)/ramdisk.image.gz System.map
	$(obj)/addRamDisk $(obj)/ramdisk.image.gz System.map $(obj)/vmlinux.sm $(obj)/vmlinux.sminitrd

else
boot:	zImage
endif

$(obj)/piggyback: $(obj)/piggyback.c
	$(HOSTCC) $(HOSTCFLAGS) -DKERNELBASE=$(KERNELBASE) -o $@ $<

$(obj)/addnote: $(obj)/addnote.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

$(obj)/image.o: $(obj)/piggyback $(obj)/vmlinux.gz
	$(obj)/piggyback $(obj)/image < $(obj)/vmlinux.gz | $(BOOTAS) -o $@

$(obj)/sysmap.o: $(obj)/piggyback System.map
	$(obj)/piggyback $(obj)/sysmap < System.map | $(BOOTAS) -o $(obj)/sysmap.o

$(obj)/initrd.o: $(obj)/ramdisk.image.gz $(obj)/piggyback
	$(obj)/piggyback $(obj)/initrd < $(obj)/ramdisk.image.gz | $(BOOTAS) -o $(obj)/initrd.o

$(obj)/zImage: $(OBJS) $(obj)/no_initrd.o $(obj)/addnote
	$(BOOTLD) $(LD_ARGS) -T $(obj)/zImage.lds -o $@ $(OBJS) $(obj)/no_initrd.o
	$(obj)/addnote $@

$(obj)/zImage.initrd: $(OBJS) $(obj)/initrd.o $(obj)/addnote
	$(BOOTLD) $(LD_ARGS) -T $(obj)/zImage.lds -o $@ $(OBJS) $(obj)/initrd.o
	./addnote $@

$(obj)/vmlinux.gz: vmlinux
	$(OBJCOPY) $(OBJCOPYFLAGS) vmlinux $(obj)/vmlinux
	ls -l $(obj)/vmlinux | awk '{printf "/* generated -- do not edit! */\nint uncompressed_size = %d;\n", $$5}' > $(obj)/imagesize.c
	$(CROSS_COMPILE)nm -n vmlinux | tail -1 | awk '{printf "long vmlinux_end = 0x%s;\n", substr($$1,8)}' >> $(obj)/imagesize.c
	gzip -vf9 $(obj)/vmlinux

$(obj)/imagesize.c: $(obj)/vmlinux.gz

clean:
	@rm -f $(OBJS)
	@rm -f $(addprefix $(obj)/,piggyback note addnote zImage \
		zImage.initrd vmlinux.gz no_initrd.o imagesize.c addSystemMap
		vmlinux.sm addRamDisk vmlinux.initrd vmlinux.sminitrd)
