#
# Makefile for the linux s390-specific parts of the memory manager.
#

targets		:= image listing
EXTRA_AFLAGS	:= -traditional

quiet_cmd_listing = OBJDUMP $@
      cmd_listing = $(OBJDUMP) --disassemble --disassemble-all \
			       --disassemble-zeroes --reloc vmlinux > $@

$(obj)/image: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/listing: vmlinux FORCE
	$(call if_changed,listing)


install: $(CONFIGURE) $(obj)/image
	sh -x $(obj)/install.sh $(KERNELRELEASE) $(obj)/image \
	      System.map Kerntypes "$(INSTALL_PATH)"
