#
# inflate/Makefile
#
#
# NOTE NOTE NOTE!
#
# This code is basically a bastardized version of the zlib
# library uncompression. It avoids dynamic allocations at
# all cost, and is cut down in other ways too, to make it
# simpler and more specialized. If you want to get the real
# thing, don't look here.
#
# The simplifications mean that this version of the library
# (unlike the real lib) is completely single-threaded, and
# you cannot do multiple uncompressions at a time. You can
# ONLY use it to uncompress a single block, with both the
# source and the destination completely in memory. In SMP
# environments, the uncompression has to be protected by
# some lock to guarantee single-threaded access to the static
# data structures used for inflation.
#
# You have been warned.
#
# (The upsides of the simplification is that you can't get in
# any nasty situations wrt memory management, and that the
# uncompression can be done without blocking on allocation).
#

O_TARGET := zlib.o

obj-y := adler32.o infblock.o infcodes.o inffast.o inflate.o \
         inftrees.o infutil.o uncompr.o

include $(TOPDIR)/Rules.make
