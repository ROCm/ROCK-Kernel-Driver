#
# Makefile for i386-specific library files..
#


lib-y = checksum.o delay.o \
	usercopy.o getuser.o \
	memcpy.o strstr.o

lib-$(CONFIG_X86_USE_3DNOW) += mmx.o
lib-$(CONFIG_HAVE_DEC_LOCK) += dec_and_lock.o
lib-$(CONFIG_DEBUG_IOVIRT)  += iodebug.o
