#
# Makefile for the Philips Nino ramdisk
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

ramdisk.o: ramdisk.gz ld.script
	$(LD) -T ld.script -b binary -o $@ ramdisk.gz

include $(TOPDIR)/Rules.make
