#
# Makefile for the agpgart device driver.  This driver adds a user
# space ioctl interface to use agp memory.  It also adds a kernel interface
# that other drivers could use to manipulate agp memory.

export-objs := agp.o

agpgart-y := agp.o frontend.o

agpgart-$(CONFIG_AGP_INTEL)	+= i8x0-agp.o
agpgart-$(CONFIG_AGP_I810)	+= i810-agp.o
agpgart-$(CONFIG_AGP_VIA)	+= via-agp.o
agpgart-$(CONFIG_AGP_AMD)	+= amd-agp.o
agpgart-$(CONFIG_AGP_SIS)	+= sis-agp.o
agpgart-$(CONFIG_AGP_ALI)	+= ali-agp.o
agpgart-$(CONFIG_AGP_SWORKS)	+= sworks-agp.o
agpgart-$(CONFIG_AGP_I460)	+= i460-agp.o
agpgart-$(CONFIG_AGP_HP_ZX1)	+= hp-agp.o
agpgart-$(CONFIG_AGP_AMD_8151)	+= k8-agp.o
agpgart-objs := $(agpgart-y)

obj-$(CONFIG_AGP) += agpgart.o

include $(TOPDIR)/Rules.make
