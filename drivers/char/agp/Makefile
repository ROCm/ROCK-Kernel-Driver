#
# Makefile for the agpgart device driver.  This driver adds a user
# space ioctl interface to use agp memory.  It also adds a kernel interface
# that other drivers could use to manipulate agp memory.

export-objs := agp.o

agpgart-y := agp.o frontend.o

agpgart-$(CONFIG_AGP_INTEL)	+= i8x0.o
agpgart-$(CONFIG_AGP_I810)	+= i810.o
agpgart-$(CONFIG_AGP_VIA)	+= via.o
agpgart-$(CONFIG_AGP_AMD)	+= amd.o
agpgart-$(CONFIG_AGP_SIS)	+= sis.o
agpgart-$(CONFIG_AGP_ALI)	+= ali.o
agpgart-$(CONFIG_AGP_SWORKS)	+= sworks.o
agpgart-$(CONFIG_AGP_I460)	+= i460.o
agpgart-$(CONFIG_AGP_HP_ZX1)	+= hp.o
agpgart-objs := $(agpgart-y)

obj-$(CONFIG_AGP) += agpgart.o

include $(TOPDIR)/Rules.make
