#
# linux/arch/arm/boot/bootp/Makefile
#

ZSYSTEM		=$(TOPDIR)/arch/arm/boot/zImage
INITRD		=$(ZSYSTEM)
ZLDFLAGS	=-p -X -T bootp.lds \
		 --defsym initrd_addr=$(INITRD_PHYS) \
		 --defsym initrd_virt=$(INITRD_VIRT) \
		 --defsym params=$(PARAMS_PHYS)

all:		bootp

# Note that bootp.lds picks up kernel.o and initrd.o
bootp:		init.o kernel.o initrd.o bootp.lds
		$(LD) $(ZLDFLAGS) -o $@ init.o

kernel.o:	$(ZSYSTEM)
		$(LD) -r -s -o $@ -b binary $(ZSYSTEM)

initrd.o:	$(INITRD)
		$(LD) -r -s -o $@ -b binary $(INITRD)

.PHONY:		$(INITRD) $(ZSYSTEM)

clean:;		$(RM) bootp
