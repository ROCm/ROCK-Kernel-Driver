#
# Makefile for the SiS framebuffer device driver
#

export-objs := sis_main.o

obj-$(CONFIG_FB_SIS) += sisfb.o

sisfb-objs := sis_main.o sis_accel.o init.o init301.o

include $(TOPDIR)/Rules.make


