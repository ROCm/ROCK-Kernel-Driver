#
# Makefile for the joystick drivers.
#

O_TARGET	:= js.o

# Objects that export symbols.

export-objs	:= serio.o gameport.o

# I-Force may need both USB and RS-232

ifeq ($(CONFIG_INPUT_IFORCE_232),m)
	ifeq ($(CONFIG_INPUT_IFORCE_USB),y)
		CONFIG_INPUT_IFORCE_USB := m
	endif
endif
ifeq ($(CONFIG_INPUT_IFORCE_USB),m)
	ifeq ($(CONFIG_INPUT_IFORCE_232),y)
		CONFIG_INPUT_IFORCE_232 := m
	endif
endif

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Each configuration option enables a list of files.

obj-$(CONFIG_INPUT_SERPORT)	+= serport.o serio.o

obj-$(CONFIG_INPUT_NS558)	+= ns558.o gameport.o
obj-$(CONFIG_INPUT_LIGHTNING)	+= lightning.o gameport.o
obj-$(CONFIG_INPUT_PCIGAME)	+= pcigame.o gameport.o

obj-$(CONFIG_INPUT_WARRIOR)	+= warrior.o serio.o
obj-$(CONFIG_INPUT_MAGELLAN)	+= magellan.o serio.o
obj-$(CONFIG_INPUT_SPACEORB)	+= spaceorb.o serio.o
obj-$(CONFIG_INPUT_SPACEBALL)	+= spaceball.o serio.o
obj-$(CONFIG_INPUT_IFORCE_232)	+= iforce.o serio.o
obj-$(CONFIG_INPUT_IFORCE_USB)	+= iforce.o

obj-$(CONFIG_INPUT_ANALOG)	+= analog.o gameport.o
obj-$(CONFIG_INPUT_A3D)		+= a3d.o gameport.o
obj-$(CONFIG_INPUT_ADI)		+= adi.o gameport.o
obj-$(CONFIG_INPUT_COBRA)	+= cobra.o gameport.o
obj-$(CONFIG_INPUT_GF2K)	+= gf2k.o gameport.o
obj-$(CONFIG_INPUT_GRIP)	+= grip.o gameport.o
obj-$(CONFIG_INPUT_INTERACT)	+= interact.o gameport.o
obj-$(CONFIG_INPUT_TMDC)	+= tmdc.o gameport.o
obj-$(CONFIG_INPUT_SIDEWINDER)	+= sidewinder.o gameport.o

obj-$(CONFIG_INPUT_DB9)		+= db9.o
obj-$(CONFIG_INPUT_GAMECON)	+= gamecon.o
obj-$(CONFIG_INPUT_TURBOGRAFX)	+= turbografx.o

obj-$(CONFIG_INPUT_AMIJOY)	+= amijoy.o

# The global Rules.make.

include $(TOPDIR)/Rules.make
