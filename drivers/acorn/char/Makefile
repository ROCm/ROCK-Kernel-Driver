#
# Makefile for the acorn character device drivers.
#

# All the objects that export symbols.
export-objs	:= mouse_rpc.o

obj-arc		:= keyb_arc.o
obj-rpc		:= keyb_ps2.o
obj-clps7500	:= keyb_ps2.o defkeymap-acorn.o

obj-$(CONFIG_RPCMOUSE)		+= mouse_rpc.o
obj-$(CONFIG_ATOMWIDE_SERIAL)	+= serial-atomwide.o
obj-$(CONFIG_DUALSP_SERIAL)	+= serial-dualsp.o
obj-$(CONFIG_ARCH_ACORN)	+= defkeymap-acorn.o i2c.o pcf8583.o
obj-$(CONFIG_L7200_KEYB)	+= defkeymap-l7200.o keyb_l7200.o

# Do the i2c and rtc last
obj-y		+= $(obj-$(MACHINE))

include $(TOPDIR)/Rules.make

%.c: %.map
	loadkeys --mktable $< > $@
