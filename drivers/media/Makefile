#
# Makefile for the kernel multimedia device drivers.
#

subdir-y     := video radio
mod-subdirs  := video radio

obj-y        := $(join $(subdir-y),$(subdir-y:%=/%.o))

include $(TOPDIR)/Rules.make
