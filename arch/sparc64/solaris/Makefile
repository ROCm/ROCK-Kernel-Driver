#
# Makefile for the Solaris binary emulation.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

ifeq ($(CONFIG_SOLARIS_EMUL),m)
CPPFLAGS = $(MODFLAGS)
endif

.S.s:
	$(CPP) $(AFLAGS) $(CPPFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) $(AFLAGS) $(CPPFLAGS) -ansi -c $< -o $*.o

list-multi := solaris.o

solaris-objs := entry64.o fs.o misc.o signal.o systbl.o socket.o \
		ioctl.o ipc.o socksys.o timod.o

obj-$(CONFIG_SOLARIS_EMUL) += solaris.o

ifneq ($(CONFIG_SOLARIS_EMUL),y)
do_it_all:
endif

solaris.o: $(solaris-objs)
	$(LD) -r -o $@ $(solaris-objs)

include $(TOPDIR)/Rules.make
