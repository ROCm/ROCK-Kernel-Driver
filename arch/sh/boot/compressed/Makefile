#
# linux/arch/sh/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

targets		:= vmlinux vmlinux.bin vmlinux.bin.gz head.o misc.o piggy.o
EXTRA_AFLAGS	:= -traditional

OBJECTS = $(obj)/head.o $(obj)/misc.o

ifdef CONFIG_SH_STANDARD_BIOS
OBJECTS += $(obj)/../../kernel/sh_bios.o
endif

#
# IMAGE_OFFSET is the load offset of the compression loader
#
IMAGE_OFFSET = $(shell printf "0x%8x" $$[0x80000000+0x$(CONFIG_MEMORY_START)+0x200000+0x10000])

LDFLAGS_vmlinux := -Ttext $(IMAGE_OFFSET) -e startup -T $(obj)/../../vmlinux.lds.s

$(obj)/vmlinux: $(OBJECTS) $(obj)/piggy.o FORCE
	$(call if_changed,ld)
	@:

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

LDFLAGS_piggy.o := -r --format binary --oformat elf32-sh-linux -T
OBJCOPYFLAGS += -R .empty_zero_page

$(obj)/piggy.o: $(obj)/vmlinux.scr $(obj)/vmlinux.bin.gz FORCE
	$(call if_changed,ld)
