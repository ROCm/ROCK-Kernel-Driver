#
# linux/arch/i386/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

HEAD = head.o

OBJECTS = $(HEAD) misc.o

#
# ZIMAGE_OFFSET is the load offset of the compression loader
# BZIMAGE_OFFSET is the load offset of the high loaded compression loader
#
EXTRA_TARGETS	:= vmlinux.bin vmlinux.bin.gz head.o misc.o
LDFLAGS_vmlinux	:= -Ttext $(IMAGE_OFFSET) -e startup_32
EXTRA_AFLAGS	:= -traditional

include $(TOPDIR)/Rules.make

vmlinux: piggy.o $(OBJECTS)
	$(LD) $(LDFLAGS) $(LDFLAGS_$@) -o vmlinux $(OBJECTS) piggy.o

vmlinux.bin: $(TOPDIR)/vmlinux FORCE
	$(call if_changed,objcopy)

vmlinux.bin.gz: vmlinux.bin FORCE
	$(call if_changed,gzip)

piggy.o: vmlinux.bin.gz vmlinux.scr
	$(LD) $(LDFLAGS) -r -o piggy.o -b binary $< -b elf32-i386 -T vmlinux.scr

clean:
	@rm -f vmlinux bvmlinux vmlinux.bin vmlinux.bin.gz
