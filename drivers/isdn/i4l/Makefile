# Makefile for the kernel ISDN subsystem and device drivers.

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN)		+= isdn.o
obj-$(CONFIG_ISDN_PPP_BSDCOMP)	+= isdn_bsdcomp.o

# Multipart objects.

isdn-y				:= isdn_net_lib.o isdn_fsm.o isdn_tty.o \
				   isdn_v110.o isdn_common.o
isdn-$(CONFIG_ISDN_NET_SIMPLE)	+= isdn_net.o
isdn-$(CONFIG_ISDN_NET_CISCO)	+= isdn_ciscohdlck.o
isdn-$(CONFIG_ISDN_PPP)		+= isdn_ppp.o isdn_ppp_ccp.o
isdn-$(CONFIG_ISDN_PPP_VJ)	+= isdn_ppp_vj.o
isdn-$(CONFIG_ISDN_MPP)		+= isdn_ppp_mp.o
isdn-$(CONFIG_ISDN_X25)		+= isdn_concap.o isdn_x25iface.o
isdn-$(CONFIG_ISDN_AUDIO)	+= isdn_audio.o
isdn-$(CONFIG_ISDN_TTY_FAX)	+= isdn_ttyfax.o
