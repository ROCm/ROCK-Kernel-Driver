#
# linux/arch/i386/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

EXTRA_TARGETS	:= vmlinux vmlinux.bin vmlinux.bin.gz head.o misc.o piggy.o
EXTRA_AFLAGS	:= -traditional

include $(TOPDIR)/Rules.make

LDFLAGS_vmlinux := -Ttext $(IMAGE_OFFSET) -e startup_32

vmlinux: head.o misc.o piggy.o FORCE
	$(call if_changed,ld)

vmlinux.bin: $(TOPDIR)/vmlinux FORCE
	$(call if_changed,objcopy)

vmlinux.bin.gz: vmlinux.bin FORCE
	$(call if_changed,gzip)

LDFLAGS_piggy.o := -r --format binary --oformat elf32-i386 -T

piggy.o: vmlinux.scr vmlinux.bin.gz FORCE
	$(call if_changed,ld)

clean:
	@rm -f vmlinux vmlinux.bin vmlinux.bin.gz
