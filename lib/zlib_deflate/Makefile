#
# This is a modified version of zlib, which does all memory
# allocation ahead of time.
#
# This is the compression code, see zlib_inflate for the
# decompression code.
#

export-objs := deflate_syms.o

obj-$(CONFIG_ZLIB_DEFLATE) += zlib_deflate.o

zlib_deflate-objs := deflate.o deftree.o deflate_syms.o

include $(TOPDIR)/Rules.make
