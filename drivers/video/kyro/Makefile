#
# Makefile for the Kyro framebuffer driver
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

obj-$(CONFIG_FB_KYRO)	+= kyrofb.o

kyrofb-objs	:= STG4000Ramdac.o STG4000VTG.o STG4000OverlayDevice.o \
		   STG4000InitDevice.o fbdev.o
