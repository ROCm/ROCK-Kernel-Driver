#
# Makefile for the linux kernel.
#

O_TARGET		:= sa1100.o

# Common support (must be linked before board specific support)
obj-y := generic.o irq.o dma.o
obj-m :=
obj-n :=
obj-  :=
led-y := leds.o

export-objs :=	dma.o generic.o irq.o pcipool.o sa1111.o sa1111-pcibuf.o \
		usb_ctl.o usb_recv.o usb_send.o pm.o

# This needs to be cleaned up.  We probably need to have SA1100
# and SA1110 config symbols.
#
# We link the CPU support next, so that RAM timings can be tuned.
ifeq ($(CONFIG_CPU_FREQ),y)
obj-$(CONFIG_SA1100_ASSABET)		+= cpu-sa1110.o
obj-$(CONFIG_SA1100_CERF)		+= cpu-sa1110.o
obj-$(CONFIG_SA1100_LART)		+= cpu-sa1100.o
obj-$(CONFIG_SA1100_PT_SYSTEM3)		+= cpu-sa1110.o
endif

# Next, the SA1111 stuff.
obj-$(CONFIG_SA1111)			+= sa1111.o
obj-$(CONFIG_USB_OHCI_HCD)		+= sa1111-pcibuf.o pcipool.o

# Specific board support
obj-$(CONFIG_SA1100_ADSBITSY)		+= adsbitsy.o
led-$(CONFIG_SA1100_ADSBITSY)		+= leds-adsbitsy.o

obj-$(CONFIG_SA1100_ASSABET)		+= assabet.o
export-objs				+= assabet.o
led-$(CONFIG_SA1100_ASSABET)		+= leds-assabet.o
obj-$(CONFIG_ASSABET_NEPONSET)		+= neponset.o

obj-$(CONFIG_SA1100_BADGE4)		+= badge4.o
export-objs				+= badge4.o
led-$(CONFIG_SA1100_BADGE4)		+= leds-badge4.o

obj-$(CONFIG_SA1100_BRUTUS)		+= brutus.o
led-$(CONFIG_SA1100_BRUTUS)		+= leds-brutus.o

obj-$(CONFIG_SA1100_CERF)		+= cerf.o
led-$(CONFIG_SA1100_CERF)		+= leds-cerf.o

obj-$(CONFIG_SA1100_EMPEG)		+= empeg.o

obj-$(CONFIG_SA1100_FLEXANET)		+= flexanet.o
export-objs				+= flexanet.o
led-$(CONFIG_SA1100_FLEXANET)		+= leds-flexanet.o

obj-$(CONFIG_SA1100_FREEBIRD)		+= freebird.o
export-objs				+= freebird.o

obj-$(CONFIG_SA1100_GRAPHICSCLIENT)	+= graphicsclient.o
led-$(CONFIG_SA1100_GRAPHICSCLIENT)	+= leds-graphicsclient.o

obj-$(CONFIG_SA1100_GRAPHICSMASTER)	+= graphicsmaster.o
led-$(CONFIG_SA1100_GRAPHICSMASTER)	+= leds-graphicsmaster.o

obj-$(CONFIG_SA1100_H3600)		+= h3600.o
export-objs				+= h3600.o

obj-$(CONFIG_SA1100_HUW_WEBPANEL)	+= huw_webpanel.o
export-objs				+= huw_webpanel.o

obj-$(CONFIG_SA1100_ITSY)		+= itsy.o

obj-$(CONFIG_SA1100_JORNADA720)		+= jornada720.o

obj-$(CONFIG_SA1100_LART)		+= lart.o
led-$(CONFIG_SA1100_LART)		+= leds-lart.o

obj-$(CONFIG_SA1100_NANOENGINE)		+= nanoengine.o

obj-$(CONFIG_SA1100_OMNIMETER)		+= omnimeter.o

obj-$(CONFIG_SA1100_PANGOLIN)		+= pangolin.o

obj-$(CONFIG_SA1100_PFS168)		+= pfs168.o
led-$(CONFIG_SA1100_PFS168)		+= leds-pfs168.o

obj-$(CONFIG_SA1100_PLEB)		+= pleb.o

obj-$(CONFIG_SA1100_PT_SYSTEM3)		+= system3.o
led-$(CONFIG_SA1100_PT_SYSTEM3)		+= leds-system3.o

obj-$(CONFIG_SA1100_SHANNON)		+= shannon.o

obj-$(CONFIG_SA1100_SHERMAN)		+= sherman.o

obj-$(CONFIG_SA1100_SIMPAD)		+= simpad.o
led-$(CONFIG_SA1100_SIMPAD)		+= leds-simpad.o  

obj-$(CONFIG_SA1100_STORK)		+= stork.o
export-objs				+= stork.o

obj-$(CONFIG_SA1100_XP860)		+= xp860.o

obj-$(CONFIG_SA1100_YOPY)		+= yopy.o
export-objs				+= yopy.o

# LEDs support
obj-$(CONFIG_LEDS) += $(led-y)

# SA1110 USB client support
sa1100usb_core-objs := usb_ctl.o usb_ep0.o usb_recv.o usb_send.o
obj-$(CONFIG_SA1100_USB) += sa1100usb_core.o
obj-$(CONFIG_SA1100_USB_NETLINK) += usb-eth.o
obj-$(CONFIG_SA1100_USB_CHAR) += usb-char.o

# Miscelaneous functions
obj-$(CONFIG_PM) += pm.o sleep.o

include $(TOPDIR)/Rules.make
