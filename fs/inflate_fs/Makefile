#
# This is a modified version of zlib, which does all memory
# allocation ahead of time.
#
# Currently only decompression is supported.
#
# Decompression needs to be serialized for each memory
# allocation.
#
#
# (The upsides of the simplification is that you can't get in
# any nasty situations wrt memory management, and that the
# uncompression can be done without blocking on allocation).
#
# The modules are named *_fs.o to distinguish from other modified
# compression libraries, like the one used by ppp.
#
# It is expected that when a deflate module is added it will be
# a separate module in a deflate_fs directory, to avoid having to
# load the deflate code for readonly filesystems.
#

O_TARGET    := inflate_fs.o

export-objs := inflate_syms.o

obj-y := adler32.o infblock.o infcodes.o inffast.o inflate.o \
	 inftrees.o infutil.o inflate_syms.o
obj-m := $(O_TARGET)

EXTRA_CFLAGS += -I $(TOPDIR)/fs/inflate_fs

include $(TOPDIR)/Rules.make
