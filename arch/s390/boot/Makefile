#
# arch/s390/boot/Makefile
#

OBJCOPY = $(CROSS_COMPILE)objcopy

EXTRA_AFLAGS := -traditional

include $(TOPDIR)/Rules.make

%.lnk: %.o
	$(LD) -Ttext 0x0 -o $@ $<

%.boot: %.lnk
	$(OBJCOPY) -O binary $< $@

image: $(TOPDIR)/vmlinux \
	iplfba.boot ipleckd.boot ipldump.boot
	$(OBJCOPY) -O binary $(TOPDIR)/vmlinux image
	$(NM) $(TOPDIR)/vmlinux | grep -v '\(compiled\)\|\( [aUw] \)\|\(\.\)\|\(LASH[RL]DI\)' | sort > $(TOPDIR)/System.map

listing: ../../../vmlinux
	$(OBJDUMP) --disassemble --disassemble-all --disassemble-zeroes --reloc $(TOPDIR)/vmlinux > listing

dep:

clean:
	rm -f image listing iplfba.boot ipleckd.boot ipldump.boot

install: $(CONFIGURE) $(BOOTIMAGE)
	sh -x ./install.sh $(KERNELRELEASE) $(BOOTIMAGE) $(TOPDIR)/System.map $(TOPDIR)/Kerntypes "$(INSTALL_PATH)"

