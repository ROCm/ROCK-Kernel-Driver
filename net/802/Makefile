#
# Makefile for the Linux 802.x protocol layers.
#

export-objs		:= llc_macinit.o p8022.o psnap.o

obj-y			:= p8023.o

obj-$(CONFIG_SYSCTL)	+= sysctl_net_802.o
obj-$(CONFIG_LLC)	+= p8022.o psnap.o llc_sendpdu.o llc_utility.o \
			   		   cl2llc.o llc_macinit.o	
obj-$(CONFIG_TR)	+= p8022.o psnap.o tr.o
obj-$(CONFIG_NET_FC)	+=                 fc.o
obj-$(CONFIG_FDDI)	+=                 fddi.o
obj-$(CONFIG_HIPPI)	+=                 hippi.o
obj-$(CONFIG_IPX)	+= p8022.o psnap.o
obj-$(CONFIG_ATALK)	+= p8022.o psnap.o

include $(TOPDIR)/Rules.make

# Dependencies on generated files need to be listed explicitly

cl2llc.o: transit/pdutr.h transit/timertr.h pseudo/pseudocode.h

# Generated files

transit/pdutr.h: transit/pdutr.pre transit/compile.awk			
	awk -f transit/compile.awk $< >$@

transit/timertr.h: transit/timertr.pre transit/compile.awk
	awk -f transit/compile.awk $< >$@

pseudo/pseudocode.h: pseudo/pseudocode pseudo/opcd2num.sed \
		     pseudo/compile.awk
	sed -f pseudo/opcd2num.sed $< | awk -f pseudo/compile.awk >$@

cl2llc.c: cl2llc.pre pseudo/opcd2num.sed
	sed -f pseudo/opcd2num.sed $< >$@
