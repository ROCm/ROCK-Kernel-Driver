#
# Makefile for the Philips Nino specific parts of the kernel
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) $(AFLAGS) $< -o $@
.S.o:
	$(CC) $(AFLAGS) -c $< -o $@

O_TARGET := nino.o

all: nino.o

obj-y	 := int-handler.o setup.o irq.o time.o reset.o rtc.o prom.o power.o

int-handler.o: int-handler.S

obj-$(CONFIG_REMOTE_DEBUG)	+= kgdb.o

obj-$(CONFIG_BLK_DEV_INITRD)	+= ramdisk.o

ramdisk.o:
		$(MAKE) -C ramdisk
		mv ramdisk/ramdisk.o ramdisk.o

clean:
		rm -f *.o

include $(TOPDIR)/Rules.make
