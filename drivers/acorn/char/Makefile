#
# Makefile for the acorn character device drivers.
#

obj-arc		:= keyb_arc.o defkeymap-acorn.o

obj-$(CONFIG_ARCH_ACORN)	+= i2c.o pcf8583.o
obj-$(CONFIG_L7200_KEYB)	+= defkeymap-l7200.o keyb_l7200.o
obj-y				+= $(obj-$(MACHINE))

$(obj)/defkeymap-acorn.o: $(obj)/defkeymap-acorn.c

# Uncomment if you're changing the keymap and have an appropriate
# loadkeys version for the map. By default, we'll use the shipped
# versions.
# GENERATE_KEYMAP := 1

ifdef GENERATE_KEYMAP

$(obj)/defkeymap-acorn.c: $(obj)/%.c: $(src)/%.map
	loadkeys --mktable $< > $@

endif
