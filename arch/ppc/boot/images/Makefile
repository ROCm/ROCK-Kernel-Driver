#
# This dir holds all of the images for PPC machines.
# Tom Rini	January 2001

all:	$(obj)/vmlinux.gz

include $(TOPDIR)/Rules.make

GZIP_FLAGS = -v9f

$(obj)/vmlinux.gz: vmlinux
	$(OBJCOPY) -S -O binary vmlinux $(obj)/vmlinux
	gzip $(GZIP_FLAGS) $(obj)/vmlinux

clean:
	rm -f $(obj)/sImage $(obj)/vmapus $(obj)/vmlinux* $(obj)/miboot* 
	rm -f $(obj)/zImage* z$(obj)/vmlinux*
