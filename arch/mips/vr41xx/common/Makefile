#
# Makefile for common code of the NEC VR4100 series.
#

obj-y				+= bcu.o cmu.o giu.o icu.o int-handler.o reset.o
obj-$(CONFIG_SERIAL_8250)	+= serial.o
obj-$(CONFIG_VR41XX_TIME_C)	+= time.o
obj-$(CONFIG_VRC4173)		+= vrc4173.o

EXTRA_AFLAGS := $(CFLAGS)
