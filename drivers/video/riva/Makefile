#
# Makefile for the Riva framebuffer driver
#

O_TARGET := built-in.o

obj-$(CONFIG_FB_RIVA) += rivafb.o

rivafb-objs := fbdev.o riva_hw.o accel.o

include $(TOPDIR)/Rules.make
