#
# Makefile for some libs needed by zImage.
#

CFLAGS_kbd.o	+= -Idrivers/char

ZLIB_DIR := ../../../../lib/zlib_inflate/

lib-y := $(addprefix $(ZLIB_DIR), \
           infblock.o infcodes.o inffast.o inflate.o inftrees.o infutil.o)
lib-y += div64.o
lib-$(CONFIG_VGA_CONSOLE) += vreset.o kbd.o

ifneq ($(KBUILD_SRC),)
_make_zlib_dir := $(shell [ -d $(obj)/$(ZLIB_DIR) ] || mkdir -p $(obj)/$(ZLIB_DIR) )
endif
