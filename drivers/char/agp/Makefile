#
# Makefile for the agpgart device driver.  This driver adds a user
# space ioctl interface to use agp memory.  It also adds a kernel interface
# that other drivers could use to manipulate agp memory.

export-objs := agpgart_be.o

agpgart-y := agpgart_fe.o agpgart_be.o

agpgart-$(CONFIG_AGP_INTEL)	+=	agpgart_be-i8x0.o
agpgart-$(CONFIG_AGP_I810)	+=	agpgart_be-i810.o
agpgart-$(CONFIG_AGP_VIA)	+=	agpgart_be-via.o
agpgart-$(CONFIG_AGP_AMD)	+=	agpgart_be-amd.o
agpgart-$(CONFIG_AGP_SIS)	+=	agpgart_be-sis.o
agpgart-$(CONFIG_AGP_ALI)	+=	agpgart_be-ali.o
agpgart-$(CONFIG_AGP_SWORKS)	+=	agpgart_be-sworks.o
agpgart-$(CONFIG_AGP_I460)	+=	agpgart_be-i460.o
agpgart-$(CONFIG_AGP_HP_ZX1)	+=	agpgart_be-hp.o
agpgart-objs := $(agpgart-y)

obj-$(CONFIG_AGP) += agpgart.o

include $(TOPDIR)/Rules.make
