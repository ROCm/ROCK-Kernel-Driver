#
# Makefile for the linux kernel.
#

# Common support
obj-y := common.o irq.o dma.o clocks.o mux.o bus.o gpio.o
obj-m :=
obj-n :=
obj-  :=
led-y := leds.o

# OCPI interconnect support for 1610
ifeq ($(CONFIG_ARCH_OMAP1610),y)
obj-y += ocpi.o
ifeq ($(CONFIG_OMAP_INNOVATOR),y)
obj-y += innovator1610.o
endif
endif

ifeq ($(CONFIG_ARCH_OMAP1510),y)
ifeq ($(CONFIG_OMAP_INNOVATOR),y)
obj-y += innovator1510.o fpga.o
endif
endif

# Specific board support
obj-$(CONFIG_MACH_OMAP_GENERIC) += omap-generic.o
obj-$(CONFIG_MACH_OMAP_PERSEUS2) += omap-perseus2.o

# LEDs support
led-$(CONFIG_OMAP_INNOVATOR) += leds-innovator.o
led-$(CONFIG_MACH_OMAP_PERSEUS2) += leds-perseus2.o
obj-$(CONFIG_LEDS) += $(led-y)

# kgdb support
obj-$(CONFIG_KGDB_SERIAL)	+= kgdb-serial.o
