#
# Makefile for the linux s390-specific parts of the memory manager.
#

EXTRA_AFLAGS := -traditional

include $(TOPDIR)/Rules.make

quiet_cmd_listing = OBJDUMP $(echo_target)
cmd_listing	  = $(OBJDUMP) --disassemble --disassemble-all \
			--disassemble-zeroes --reloc vmlinux > $@

$(obj)/image: vmlinux
	$(call if_changed,objcopy)

$(obj)/listing: vmlinux
	$(call if_changed,listing)

image: $(obj)/image

listing: $(obj)/listing

clean:
	rm -f $(obj)/image $(obj)/listing

install: $(CONFIGURE) $(BOOTIMAGE)
	sh -x $(obj)/install.sh $(KERNELRELEASE) $(BOOTIMAGE) System.map Kerntypes "$(INSTALL_PATH)"
