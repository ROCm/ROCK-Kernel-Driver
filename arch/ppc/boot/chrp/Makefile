# BK Id: SCCS/s.Makefile 1.13 07/27/01 20:24:17 trini
#
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

LD_ARGS = -Ttext 0x00400000

OBJS = ../common/crt0.o start.o main.o misc.o ../common/string.o image.o \
	../common/ofcommon.o
LIBS = $(TOPDIR)/lib/lib.a ../lib/zlib.a
ADDNOTE = ../utils/addnote
PIGGYBACK = ../utils/piggyback

ifeq ($(CONFIG_SMP),y)
TFTPIMAGE=/tftpboot/zImage.chrp.smp$(MSIZE)
else
TFTPIMAGE=/tftpboot/zImage.chrp$(MSIZE)
endif

all: zImage

znetboot: zImage
ifdef CONFIG_SMP
	cp -f $(TOPDIR)/vmlinux /tftpboot/vmlinux.smp
else
	cp -f $(TOPDIR)/vmlinux /tftpboot/vmlinux
endif
ifdef CONFIG_PPC64BRIDGE
	cp -f $(TOPDIR)/vmlinux /tftpboot/vmlinux.64
endif
	cp ../images/zImage.chrp $(TFTPIMAGE)

znetboot.initrd: zImage.initrd
	cp ../images/zImage.initrd.chrp $(TFTPIMAGE)

floppy: zImage
	mcopy zImage a:zImage

image.o: $(PIGGYBACK) ../images/vmlinux.gz
	$(PIGGYBACK) image < ../images/vmlinux.gz | $(AS) -o $@

sysmap.o: $(PIGGYBACK) $(TOPDIR)/System.map
	$(PIGGYBACK) sysmap < $(TOPDIR)/System.map | $(AS) -o $@

initrd.o: ../images/ramdisk.image.gz $(PIGGYBACK)
	$(PIGGYBACK) initrd < ../images/ramdisk.image.gz | $(AS) -o $@

zImage: $(OBJS) $(LIBS) ../common/no_initrd.o $(ADDNOTE) ../images/vmlinux.gz
	$(LD) $(LD_ARGS) -o ../images/$@.chrp $(OBJS) ../common/no_initrd.o $(LIBS)
	cp ../images/$@.chrp ../images/$@.chrp-rs6k
	$(ADDNOTE) ../images/$@.chrp-rs6k

zImage.initrd: $(OBJS) $(LIBS) initrd.o $(ADDNOTE) ../images/vmlinux.gz
	$(LD) $(LD_ARGS) -o ../images/$@.chrp $(OBJS) initrd.o $(LIBS)
	cp ../images/$@.chrp ../images/$@.chrp-rs6k
	$(ADDNOTE) ../images/$@.chrp-rs6k

include $(TOPDIR)/Rules.make
