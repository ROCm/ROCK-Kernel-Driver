#
# Makefile for the Solaris binary emulation.
#

EXTRA_AFLAGS := -ansi

ifeq ($(CONFIG_SOLARIS_EMUL),m)
  EXTRA_AFLAGS += $(MODFLAGS)
endif

solaris-objs := entry64.o fs.o misc.o signal.o systbl.o socket.o \
		ioctl.o ipc.o socksys.o timod.o

obj-$(CONFIG_SOLARIS_EMUL) += solaris.o

include $(TOPDIR)/Rules.make
