#
# Makefile for the DECstation family specific parts of the kernel
#

netboot: all
	mipsel-linux-ld -N -G 0 -T ld.ecoff ../../boot/zImage \
		built-in.o ramdisk.img -o nbImage

obj-y	 := decstation.o

clean:
	rm -f nbImage

include $(TOPDIR)/Rules.make
