#
# drivers/net/tulip/Makefile
#
# Makefile for the Linux "Tulip" family network device drivers.
#

O_TARGET := tulip_net.o

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

# Things that need to export symbols
export-objs	:=

obj-$(CONFIG_PCMCIA_XIRTULIP)	+= xircom_tulip_cb.o
obj-$(CONFIG_PCMCIA_XIRCOM)	+= xircom_cb.o
obj-$(CONFIG_DM9102)		+= dmfe.o
obj-$(CONFIG_WINBOND_840)	+= winbond-840.o
obj-$(CONFIG_DE2104X)		+= de2104x.o
obj-$(CONFIG_TULIP)		+= tulip.o
obj-$(CONFIG_DE4X5)		+= de4x5.o

# Declare multi-part drivers.
list-multi			:= tulip.o

tulip-objs			:= eeprom.o interrupt.o media.o \
				   timer.o tulip_core.o		\
				   21142.o pnic.o pnic2.o

include $(TOPDIR)/Rules.make

# Link rules for multi-part drivers.
tulip.o: $(tulip-objs)
	$(LD) -r -o $@ $(tulip-objs)
