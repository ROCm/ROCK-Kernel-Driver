#
# Makefile for the input core drivers.
#

# The target object and module list name.

O_TARGET	:= inputdrv.o

# Objects that export symbols.

mod-subdirs	:= joystick
export-objs	:= input.o

# Each configuration option enables a list of files.

obj-$(CONFIG_INPUT)		+= input.o
obj-$(CONFIG_INPUT_KEYBDEV)	+= keybdev.o
obj-$(CONFIG_INPUT_MOUSEDEV)	+= mousedev.o
obj-$(CONFIG_INPUT_JOYDEV)	+= joydev.o
obj-$(CONFIG_INPUT_EVDEV)	+= evdev.o

obj-$(CONFIG_INPUT_JOYSTICK)	+= joystick/

# The global Rules.make.

include $(TOPDIR)/Rules.make
