#
# Makefile for some libs needed by zImage.
#

CFLAGS_kbd.o	+= -Idrivers/char

lib-y := zlib.o div64.o
lib-$(CONFIG_VGA_CONSOLE) += vreset.o kbd.o
