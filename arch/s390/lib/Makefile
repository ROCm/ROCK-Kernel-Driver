#
# Makefile for s390-specific library files..
#

.S.o:
	$(CC) $(AFLAGS) -traditional -c $< -o $*.o

L_TARGET = lib.a
L_OBJS = checksum.o delay.o memset.o strcmp.o strncpy.o

include $(TOPDIR)/Rules.make

