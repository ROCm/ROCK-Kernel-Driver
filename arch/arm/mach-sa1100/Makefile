#
# Makefile for the linux kernel.
#

# Common support
obj-y := generic.o irq.o dma.o
obj-m :=
obj-n :=
obj-  :=
led-y := leds.o

obj-$(CONFIG_CPU_FREQ_SA1100)		+= cpu-sa1100.o
obj-$(CONFIG_CPU_FREQ_SA1110)		+= cpu-sa1110.o

# Specific board support
obj-$(CONFIG_SA1100_ADSBITSY)		+= adsbitsy.o
led-$(CONFIG_SA1100_ADSBITSY)		+= leds-adsbitsy.o

obj-$(CONFIG_SA1100_ASSABET)		+= assabet.o
led-$(CONFIG_SA1100_ASSABET)		+= leds-assabet.o
obj-$(CONFIG_ASSABET_NEPONSET)		+= neponset.o

obj-$(CONFIG_SA1100_BADGE4)		+= badge4.o
led-$(CONFIG_SA1100_BADGE4)		+= leds-badge4.o

obj-$(CONFIG_SA1100_BRUTUS)		+= brutus.o
led-$(CONFIG_SA1100_BRUTUS)		+= leds-brutus.o

obj-$(CONFIG_SA1100_CERF)		+= cerf.o
led-$(CONFIG_SA1100_CERF)		+= leds-cerf.o

obj-$(CONFIG_SA1100_EMPEG)		+= empeg.o

obj-$(CONFIG_SA1100_FLEXANET)		+= flexanet.o
led-$(CONFIG_SA1100_FLEXANET)		+= leds-flexanet.o

obj-$(CONFIG_SA1100_FREEBIRD)		+= freebird.o
obj-$(CONFIG_SA1100_GRAPHICSCLIENT)	+= graphicsclient.o
led-$(CONFIG_SA1100_GRAPHICSCLIENT)	+= leds-graphicsclient.o

obj-$(CONFIG_SA1100_GRAPHICSMASTER)	+= graphicsmaster.o
led-$(CONFIG_SA1100_GRAPHICSMASTER)	+= leds-graphicsmaster.o

obj-$(CONFIG_SA1100_H3600)		+= h3600.o

obj-$(CONFIG_SA1100_HACKKIT)		+= hackkit.o
led-$(CONFIG_SA1100_HACKKIT)		+= leds-hackkit.o

obj-$(CONFIG_SA1100_HUW_WEBPANEL)	+= huw_webpanel.o

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

obj-$(CONFIG_SA1100_TRIZEPS)		+= trizeps.o

obj-$(CONFIG_SA1100_XP860)		+= xp860.o

obj-$(CONFIG_SA1100_YOPY)		+= yopy.o

# LEDs support
obj-$(CONFIG_LEDS) += $(led-y)

# SA1110 USB client support
#obj-$(CONFIG_SA1100_USB)		+= usb/

# Miscelaneous functions
obj-$(CONFIG_PM)			+= pm.o sleep.o
obj-$(CONFIG_SA1100_SSP)		+= ssp.o
