#
# Makefile for the Solaris binary emulation.
#

ifeq ($(CONFIG_SOLARIS_EMUL),m)
CPPFLAGS = $(MODFLAGS)
endif

.S.s:
	$(CPP) $(AFLAGS) $(CPPFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) $(AFLAGS) $(CPPFLAGS) -ansi -c $< -o $*.o

solaris-objs := entry64.o fs.o misc.o signal.o systbl.o socket.o \
		ioctl.o ipc.o socksys.o timod.o

obj-$(CONFIG_SOLARIS_EMUL) += solaris.o

include $(TOPDIR)/Rules.make
