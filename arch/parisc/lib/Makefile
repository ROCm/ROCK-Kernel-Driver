#
# Makefile for parisc-specific library files..
#


L_TARGET = lib.a
L_OBJS	= lusercopy.o bitops.o checksum.o


.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o

include $(TOPDIR)/Rules.make
