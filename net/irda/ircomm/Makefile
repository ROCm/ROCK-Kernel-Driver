#
# Makefile for the Linux IrDA IrCOMM protocol layer.
#

O_TARGET := ircomm_and_tty.o

export-objs := ircomm_core.o

ircomm-objs := ircomm_core.o ircomm_event.o ircomm_lmp.o ircomm_ttp.o
ircomm-tty-objs := ircomm_tty.o ircomm_tty_attach.o ircomm_tty_ioctl.o ircomm_param.o

obj-$(CONFIG_IRCOMM) += ircomm.o ircomm-tty.o

include $(TOPDIR)/Rules.make

