#
# Makefile for the linux kernel.
#

# Common support
obj-y := common.o irq.o dma.o clocks.o mux.o bus.o gpio.o
obj-m :=
obj-n :=
obj-  :=
led-y := leds.o

# Specific board support
obj-$(CONFIG_MACH_OMAP_INNOVATOR) += board-innovator.o
obj-$(CONFIG_MACH_OMAP_GENERIC) += board-generic.o
obj-$(CONFIG_MACH_OMAP_PERSEUS2) += board-perseus2.o
obj-$(CONFIG_MACH_OMAP_OSK) += board-osk.o

# OCPI interconnect support for 1610 and 5912
obj-$(CONFIG_ARCH_OMAP1610) += ocpi.o
obj-$(CONFIG_ARCH_OMAP5912) += ocpi.o

# LEDs support
led-$(CONFIG_MACH_OMAP_INNOVATOR) += leds-innovator.o
led-$(CONFIG_MACH_OMAP_PERSEUS2) += leds-perseus2.o
obj-$(CONFIG_LEDS) += $(led-y)

# Power Management
obj-$(CONFIG_PM) += pm.o sleep.o

ifeq ($(CONFIG_ARCH_OMAP1510),y)
# Innovator-1510 FPGA
obj-$(CONFIG_MACH_OMAP_INNOVATOR) += fpga.o
endif

# kgdb support
obj-$(CONFIG_KGDB_SERIAL)	+= kgdb-serial.o
