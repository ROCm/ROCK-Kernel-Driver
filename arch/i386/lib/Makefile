#
# Makefile for i386-specific library files..
#

.S.o:
	$(CC) $(AFLAGS) -c $< -o $*.o

L_TARGET = lib.a

obj-y = checksum.o old-checksum.o delay.o \
	usercopy.o getuser.o putuser.o iodebug.o \
	memcpy.o

obj-$(CONFIG_X86_USE_3DNOW) += mmx.o
obj-$(CONFIG_HAVE_DEC_LOCK) += dec_and_lock.o

include $(TOPDIR)/Rules.make
