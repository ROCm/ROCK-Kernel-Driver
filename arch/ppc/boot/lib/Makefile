#
# Makefile for some libs needed by zImage.
#

CFLAGS_kbd.o	+= -Idrivers/char

$(obj)/infblock.o: lib/zlib_inflate/infblock.c
	$(call cmd,cc_o_c)
$(obj)/infcodes.o: lib/zlib_inflate/infcodes.c
	$(call cmd,cc_o_c)
$(obj)/inffast.o: lib/zlib_inflate/inffast.c
	$(call cmd,cc_o_c)
$(obj)/inflate.o: lib/zlib_inflate/inflate.c
	$(call cmd,cc_o_c)
$(obj)/inftrees.o: lib/zlib_inflate/inftrees.c
	$(call cmd,cc_o_c)
$(obj)/infutil.o: lib/zlib_inflate/infutil.c
	$(call cmd,cc_o_c)

lib-y := infblock.o infcodes.o inffast.o inflate.o inftrees.o infutil.o div64.o
lib-$(CONFIG_VGA_CONSOLE) += vreset.o kbd.o
