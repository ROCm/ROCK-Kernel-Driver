#
# Makefile for ALSA
#

snd-usb-audio-objs := usbaudio.o usbmixer.o
ifeq ($(subst m,y,$(CONFIG_SND_SEQUENCER)),y)
snd-usb-midi-objs := usbmidi.o
endif

# Toplevel Module Dependency
obj-$(CONFIG_SND_USB_AUDIO) += snd-usb-audio.o
ifeq ($(subst m,y,$(CONFIG_SND_SEQUENCER)),y)
obj-$(CONFIG_SND_USB_AUDIO) += snd-usb-midi.o
endif

include $(TOPDIR)/Rules.make
