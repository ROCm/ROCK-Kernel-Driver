#
# arch/ppc/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Adapted for PowerPC by Gary Thomas
# modified by Cort (cort@cs.nmt.edu)
#
.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS)  -DINITRD_OFFSET=$(IOFF) -DINITRD_SIZE=$(ISZ) -DZIMAGE_OFFSET=$(ZOFF) -DZIMAGE_SIZE=$(ZSZ) -D__BOOTER__ -c -o $*.o $<
.S.s:
	$(CPP) $(AFLAGS) -traditional -o $*.o $<
.S.o:
	$(CC) $(AFLAGS) -traditional -c -o $*.o $<

ZOFF = 0
ZSZ = 0
IOFF = 0
ISZ = 0

ifeq ($(CONFIG_ALL_PPC),y)
CONFIG_PREP=y
endif

ifeq ($(CONFIG_SMP),y)
TFTPIMAGE=/tftpboot/zImage.prep.smp$(MSIZE)
else
TFTPIMAGE=/tftpboot/zImage.prep$(MSIZE)
endif

ifeq ($(CONFIG_SMP),y)
TFTPSIMAGE=/tftpboot/sImage.smp
else
TFTPSIMAGE=/tftpboot/sImage
endif

ifeq ($(CONFIG_PPC64BRIDGE),y)
MSIZE=.64
else
MSIZE=
endif

ZLINKFLAGS = -T ../vmlinux.lds -Ttext 0x00800000

GZIP_FLAGS = -v9f

OBJECTS := head.o misc.o ../coffboot/zlib.o
CFLAGS = $(CPPFLAGS) -O2 -DSTDC_HEADERS -fno-builtin
OBJCOPY_ARGS = -O elf32-powerpc

OBJECTS += vreset.o kbd.o of1275.o
ifeq ($(CONFIG_SERIAL_CONSOLE),y)
OBJECTS += ns16550.o
endif

all:	zImage

zvmlinux.initrd: zvmlinux
	$(LD) $(ZLINKFLAGS) -o zvmlinux.initrd.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=initrd=ramdisk.image.gz \
		--add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.initrd.tmp zvmlinux.initrd
	$(CC) $(CFLAGS) -DINITRD_OFFSET=`sh offset $(OBJDUMP) zvmlinux.initrd initrd` \
		-DINITRD_SIZE=`sh size $(OBJDUMP) zvmlinux.initrd initrd` \
		-DZIMAGE_OFFSET=`sh offset $(OBJDUMP) zvmlinux.initrd image` \
		-DZIMAGE_SIZE=`sh size $(OBJDUMP) zvmlinux.initrd image` \
		-D__BOOTER__ \
		-c -o misc.o misc.c
	$(LD) $(ZLINKFLAGS) -o zvmlinux.initrd.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment \
		--add-section=initrd=ramdisk.image.gz \
		--add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.initrd.tmp $@
	rm zvmlinux.initrd.tmp

zImage: zvmlinux mkprep sImage
ifdef CONFIG_PREP
	./mkprep -pbp zvmlinux zImage
endif
ifdef CONFIG_APUS
	$(STRIP) ../../../vmlinux -o vmapus
	gzip $(GZIP_FLAGS) vmapus
endif

sImage: ../../../vmlinux
ifdef CONFIG_GEMINI
	$(OBJCOPY) -I elf32-powerpc -O binary ../../../vmlinux sImage
endif

zImage.initrd: zvmlinux.initrd mkprep
ifdef CONFIG_PREP
	./mkprep -pbp zvmlinux.initrd zImage.initrd
endif

zvmlinux: $(OBJECTS) ../coffboot/vmlinux.gz
#
# build the boot loader image and then compute the offset into it
# for the kernel image
#
	$(LD) $(ZLINKFLAGS) -o zvmlinux.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment --add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.tmp $@
#
# then with the offset rebuild the bootloader so we know where the kernel is
#
	$(CC) $(CFLAGS) -DINITRD_OFFSET=0 -DINITRD_SIZE=0 \
		-DZIMAGE_OFFSET=`sh offset $(OBJDUMP) zvmlinux image` \
		-DZIMAGE_SIZE=`sh size $(OBJDUMP) zvmlinux image` \
		-D__BOOTER__ \
		-c -o misc.o misc.c
	$(LD) $(ZLINKFLAGS) -o zvmlinux.tmp $(OBJECTS)
	$(OBJCOPY) $(OBJCOPY_ARGS) -R .comment --add-section=image=../coffboot/vmlinux.gz \
		zvmlinux.tmp $@
	rm zvmlinux.tmp

floppy: $(TOPDIR)/vmlinux zImage
	dd if=zImage of=/dev/fd0H1440 bs=64b

mkprep : mkprep.c
	$(HOSTCC) -o mkprep mkprep.c

znetboot : zImage
ifdef CONFIG_PREP
	cp zImage $(TFTPIMAGE)
endif
ifdef CONFIG_GEMINI
	cp sImage $(TFTPSIMAGE)
endif

znetboot.initrd : zImage.initrd
	cp zImage.initrd $(TFTPIMAGE)

clean:
	rm -f vmlinux* zvmlinux* mkprep zImage* sImage*

fastdep:
	$(TOPDIR)/scripts/mkdep *.[Sch] > .depend

dep:
	$(CPP) $(CPPFLAGS) -M *.S *.c > .depend

# just here to match coffboot/Makefile
vmlinux.coff:

vmlinux.coff.initrd:
