#
# This dir holds all of the images for PPC machines.
# Tom Rini	January 2001

EXTRA_TARGETS	:= vmlinux.gz

GZIP_FLAGS = -v9f

$(obj)/vmlinux.gz: vmlinux
	$(OBJCOPY) -O binary $< $(@:.gz=)
	gzip $(GZIP_FLAGS) $(@:.gz=)

clean:
	rm -f $(obj)/sImage $(obj)/vmapus $(obj)/vmlinux* $(obj)/miboot* 
	rm -f $(obj)/zImage* z$(obj)/vmlinux*
