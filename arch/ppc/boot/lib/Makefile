#
# Makefile for some libs needed by zImage.
#

CFLAGS_kbd.o	+= -Idrivers/char

lib-y := $(addprefix ../../../../lib/zlib_inflate/, \
           infblock.o infcodes.o inffast.o inflate.o inftrees.o infutil.o)
lib-y += div64.o
lib-$(CONFIG_VGA_CONSOLE) += vreset.o kbd.o
