# Makefile for the kernel ISDN subsystem and device drivers.

# Objects that export symbols.

export-objs	:= isdn_common.o

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN)			+= isdn.o
obj-$(CONFIG_ISDN_PPP_BSDCOMP)		+= isdn_bsdcomp.o

# Multipart objects.

isdn-objs				:= isdn_net.o isdn_net_lib.o \
					   isdn_ciscohdlck.o \
					   isdn_tty.o isdn_v110.o \
					   isdn_common.o \

# Optional parts of multipart objects.

isdn-objs-$(CONFIG_ISDN_PPP)		+= isdn_ppp.o
isdn-objs-$(CONFIG_ISDN_X25)		+= isdn_concap.o isdn_x25iface.o
isdn-objs-$(CONFIG_ISDN_AUDIO)		+= isdn_audio.o
isdn-objs-$(CONFIG_ISDN_TTY_FAX)	+= isdn_ttyfax.o

isdn-objs				+= $(isdn-objs-y)

include $(TOPDIR)/Rules.make
