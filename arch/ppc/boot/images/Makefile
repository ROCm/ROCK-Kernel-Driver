#
# This dir holds all of the images for PPC machines.
# Tom Rini	January 2001

extra-y		:= vmlinux.gz
GZIP_FLAGS	:= -v9f

$(obj)/vmlinux.gz: vmlinux
	$(OBJCOPY) -O binary $< $(@:.gz=)
	gzip $(GZIP_FLAGS) $(@:.gz=)

# Files generated that shall be removed upon make clean
clean-files	:= sImage vmapus vmlinux* miboot* zImage*
