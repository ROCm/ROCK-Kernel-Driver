#
# Makefile for the Philips Nino specific parts of the kernel
#

obj-y	 := int-handler.o setup.o irq.o time.o reset.o rtc.o prom.o power.o

obj-$(CONFIG_REMOTE_DEBUG)	+= kgdb.o

obj-$(CONFIG_BLK_DEV_INITRD)	+= ramdisk.o

include $(TOPDIR)/Rules.make

ramdisk.o:
		$(MAKE) -C ramdisk
		mv ramdisk/ramdisk.o ramdisk.o

clean:
		rm -f *.o

