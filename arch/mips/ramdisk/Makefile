#
# Makefile for a ramdisk image
#

O_FORMAT = $(shell $(OBJDUMP) -i | head -n 2 | grep elf32)
img = $(CONFIG_EMBEDDED_RAMDISK_IMAGE)
ramdisk.o: $(subst ",,$(img)) ld.script
	echo "O_FORMAT:  " $(O_FORMAT)
	$(LD) -T ld.script -b binary --oformat $(O_FORMAT) -o $@ $(img)
