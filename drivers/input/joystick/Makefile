#
# Makefile for the input core drivers.
#

# The target object and module list name.

O_TARGET	:= joydrv.o

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

# Each configuration option enables a list of files.

obj-$(CONFIG_JOYSTICK_A3D)		+= a3d.o
obj-$(CONFIG_JOYSTICK_ADI)		+= adi.o
obj-$(CONFIG_JOYSTICK_AMIGA)		+= amijoy.o
obj-$(CONFIG_JOYSTICK_ANALOG)		+= analog.o
obj-$(CONFIG_JOYSTICK_COBRA)		+= cobra.o
obj-$(CONFIG_JOYSTICK_DB9)		+= db9.o
obj-$(CONFIG_JOYSTICK_GAMECON)		+= gamecon.o
obj-$(CONFIG_JOYSTICK_GF2K)		+= gf2k.o
obj-$(CONFIG_JOYSTICK_GRIP)		+= grip.o
obj-$(CONFIG_JOYSTICK_IFORCE)		+= iforce.o
obj-$(CONFIG_JOYSTICK_INTERACT)		+= interact.o
obj-$(CONFIG_JOYSTICK_MAGELLAN)		+= magellan.o
obj-$(CONFIG_JOYSTICK_SIDEWINDER)	+= sidewinder.o
obj-$(CONFIG_JOYSTICK_SPACEBALL)	+= spaceball.o
obj-$(CONFIG_JOYSTICK_SPACEORB)		+= spaceorb.o
obj-$(CONFIG_JOYSTICK_STINGER)		+= stinger.o
obj-$(CONFIG_JOYSTICK_TMDC)		+= tmdc.o
obj-$(CONFIG_JOYSTICK_TURBOGRAFX)	+= turbografx.o
obj-$(CONFIG_JOYSTICK_WARRIOR)		+= warrior.o

# The global Rules.make.

include $(TOPDIR)/Rules.make
