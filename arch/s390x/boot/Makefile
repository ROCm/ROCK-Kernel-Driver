#
# Makefile for the linux s390-specific parts of the memory manager.
#

OBJCOPY = $(CROSS_COMPILE)objcopy

O_TARGET := 

include $(TOPDIR)/Rules.make

EXTRA_AFLAGS := -traditional

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

