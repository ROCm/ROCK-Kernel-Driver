#
# linux/arch/arm/boot/bootp/Makefile
#

ZSYSTEM		= arch/arm/boot/zImage
ZLDFLAGS	=-p -X -T $(obj)/bootp.lds \
		 --defsym initrd_addr=$(INITRD_PHYS) \
		 --defsym params=$(PARAMS_PHYS)

extra-y := bootp

# Note that bootp.lds picks up kernel.o and initrd.o
$(obj)/bootp:	$(addprefix $(obj)/,init.o kernel.o initrd.o bootp.lds)
		$(LD) $(ZLDFLAGS) -o $@ $(obj)/init.o

$(obj)/kernel.o: $(ZSYSTEM)
		$(LD) -r -s -o $@ -b binary $(ZSYSTEM)

$(obj)/initrd.o: $(INITRD)
		$(LD) -r -s -o $@ -b binary $(INITRD)

.PHONY:		$(INITRD) $(ZSYSTEM)
