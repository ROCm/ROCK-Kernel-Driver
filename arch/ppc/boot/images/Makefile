#
# This dir holds all of the images for PPC machines.
# Tom Rini	January 2001

extra-y		:= vmlinux.bin vmlinux.gz

OBJCOPYFLAGS_vmlinux.bin := -O binary
$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

# Files generated that shall be removed upon make clean
clean-files	:= sImage vmapus vmlinux* miboot* zImage*
