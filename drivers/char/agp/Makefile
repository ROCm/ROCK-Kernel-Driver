#
# Makefile for the agpgart device driver.  This driver adds a user
# space ioctl interface to use agp memory.  It also adds a kernel interface
# that other drivers could use to manipulate agp memory.

agpgart-y := backend.o frontend.o generic.o generic-3.0.o
agpgart-objs := $(agpgart-y)
obj-$(CONFIG_AGP) += agpgart.o

obj-$(CONFIG_AGP_INTEL)		+= intel-agp.o
obj-$(CONFIG_AGP_VIA)		+= via-agp.o
obj-$(CONFIG_AGP_AMD)		+= amd-k7-agp.o
obj-$(CONFIG_AGP_SIS)		+= sis-agp.o
obj-$(CONFIG_AGP_ALI)		+= ali-agp.o
obj-$(CONFIG_AGP_SWORKS)	+= sworks-agp.o
obj-$(CONFIG_AGP_I460)		+= i460-agp.o
obj-$(CONFIG_AGP_HP_ZX1)	+= hp-agp.o
obj-$(CONFIG_AGP_AMD_8151)	+= amd-k8-agp.o
obj-$(CONFIG_AGP_ALPHA_CORE)	+= alpha-agp.o
obj-$(CONFIG_AGP_NVIDIA)	+= nvidia=agp.o

