#
# linux/arch/i386/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

HEAD = head.o

OBJECTS = $(HEAD) misc.o

ZLDFLAGS = -e startup_32

#
# ZIMAGE_OFFSET is the load offset of the compression loader
# BZIMAGE_OFFSET is the load offset of the high loaded compression loader
#
ZIMAGE_OFFSET = 0x1000
BZIMAGE_OFFSET = 0x100000

ZLINKFLAGS = -Ttext $(ZIMAGE_OFFSET) $(ZLDFLAGS)
BZLINKFLAGS = -Ttext $(BZIMAGE_OFFSET) $(ZLDFLAGS)

EXTRA_TARGETS	:= vmlinux.bin vmlinux.bin.gz head.o misc.o
EXTRA_AFLAGS	:= -traditional

include $(TOPDIR)/Rules.make

vmlinux: piggy.o $(OBJECTS)
	$(LD) $(LDFLAGS) $(ZLINKFLAGS) -o vmlinux $(OBJECTS) piggy.o

bvmlinux: piggy.o $(OBJECTS)
	$(LD) $(LDFLAGS) $(BZLINKFLAGS) -o bvmlinux $(OBJECTS) piggy.o

vmlinux.bin: $(TOPDIR)/vmlinux FORCE
	$(call if_changed,objcopy)

vmlinux.bin.gz: vmlinux.bin FORCE
	$(call if_changed,gzip)

piggy.o: vmlinux.bin.gz vmlinux.scr
	$(LD) $(LDFLAGS) -r -o piggy.o -b binary $< -b elf32-i386 -T vmlinux.scr

clean:
	@rm -f vmlinux bvmlinux vmlinux.bin vmlinux.bin.gz
