#
# Makefile for the linux s390-specific parts of the memory manager.
#

O_TARGET := 

EXTRA_AFLAGS := -traditional

include $(TOPDIR)/Rules.make

%.lnk: %.o
	$(LD) -Ttext 0x0 -o $@ $<

%.boot: %.lnk
	$(OBJCOPY) $(OBJCOPYFLAGS) $< $@

image: $(TOPDIR)/vmlinux \
	iplfba.boot ipleckd.boot ipldump.boot
	$(OBJCOPY) $(OBJCOPYFLAGS) $< $@
	$(NM) $(TOPDIR)/vmlinux | grep -v '\(compiled\)\|\( [aUw] \)\|\(\.\)\|\(LASH[RL]DI\)' | sort > $(TOPDIR)/System.map

listing: ../../../vmlinux
	$(OBJDUMP) --disassemble --disassemble-all --disassemble-zeroes --reloc $(TOPDIR)/vmlinux > listing

clean:
	rm -f image listing iplfba.boot ipleckd.boot ipldump.boot

install: $(CONFIGURE) $(BOOTIMAGE)
	sh -x ./install.sh $(KERNELRELEASE) $(BOOTIMAGE) $(TOPDIR)/System.map $(TOPDIR)/Kerntypes "$(INSTALL_PATH)"
