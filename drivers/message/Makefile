#
# Makefile for MPT based block devices
#

O_TARGET := message.o

mod-subdirs := i2o fusion 

subdir-$(CONFIG_I2O)		+= i2o
ifeq ($(CONFIG_I2O),y)
  obj-y += i2o/i2o.o
endif

subdir-$(CONFIG_FUSION)		+= fusion
ifeq ($(CONFIG_FUSION),y)
  obj-y += fusion/fusion.o
endif

include $(TOPDIR)/Rules.make

