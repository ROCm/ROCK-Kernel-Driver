#
# Makefile for the I-Force driver
#

# I-Force may need both USB and RS-232

CONFIG_JOYSTICK_IFORCE := n

ifeq ($(CONFIG_JOYSTICK_IFORCE_232),y)
	ifeq ($(CONFIG_JOYSTICK_IFORCE_USB),y)
		CONFIG_JOYSTICK_IFORCE := y
	endif
endif

ifeq ($(CONFIG_JOYSTICK_IFORCE_232),m)
	CONFIG_JOYSTICK_IFORCE := m
endif

ifeq ($(CONFIG_JOYSTICK_IFORCE_USB),m)
	CONFIG_JOYSTICK_IFORCE := m
endif

obj-$(CONFIG_JOYSTICK_IFORCE)	+= iforce.o 

# The global Rules.make.

include $(TOPDIR)/Rules.make

# iforce.o is a multipart module.

IFORCEOBJS = iforce-ff.o iforce-main.o iforce-packets.o

ifneq ($(CONFIG_JOYSTICK_IFORCE_232),)
	IFORCEOBJS += iforce-serio.o 
endif

ifneq ($(CONFIG_JOYSTICK_IFORCE_USB),)
	IFORCEOBJS += iforce-usb.o 
endif

iforce.o: $(IFORCEOBJS) 
	$(LD) -i $(IFORCEOBJS) -o $@
