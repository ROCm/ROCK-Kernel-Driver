#
# Makefile for MPT based block devices
#

O_TARGET := message.o

mod-subdirs := i2o fusion 

obj-$(CONFIG_I2O)	+= i2o/
obj-$(CONFIG_FUSION)	+= fusion/

include $(TOPDIR)/Rules.make

