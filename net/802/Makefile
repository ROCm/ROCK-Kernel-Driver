#
# Makefile for the Linux 802.x protocol layers.
#

export-objs		:= llc_macinit.o p8022.o psnap.o

obj-y			:= p8023.o

# Check the p8022 selections against net/core/Makefile.
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

$(obj)/cl2llc.o: $(obj)/transit/pdutr.h $(obj)/transit/timertr.h \
		 $(obj)/pseudo/pseudocode.h

# Generated files

$(obj)/transit/pdutr.h: $(src)/transit/pdutr.pre $(src)/transit/compile.awk
	awk -f $(src)/transit/compile.awk $< >$@

$(obj)/transit/timertr.h: $(src)/transit/timertr.pre $(src)/transit/compile.awk
	awk -f $(src)/transit/compile.awk $< >$@

$(obj)/pseudo/pseudocode.h: $(src)/pseudo/pseudocode \
			    $(src)/pseudo/opcd2num.sed \
			    $(src)/pseudo/compile.awk
	sed -f $(src)/pseudo/opcd2num.sed $< | \
	awk -f $(src)/pseudo/compile.awk >$@

$(obj)/cl2llc.c: $(src)/cl2llc.pre $(src)/pseudo/opcd2num.sed
	sed -f $(src)/pseudo/opcd2num.sed $< >$@
