# Makefile for the Linux device tree

obj-$(CONFIG_EISA)	+= eisa-bus.o

export-objs	:= eisa-bus.o

clean-files:= devlist.h

# Ugly hack to get DEVICE_NAME_SIZE value...
DEVICE_NAME_SIZE:=$(shell awk '$$1=="\#define" && $$2=="DEVICE_NAME_SIZE" {print $$3-1}' $(srctree)/include/linux/device.h)

$(obj)/eisa-bus.o: $(obj)/devlist.h

$(obj)/devlist.h: $(src)/eisa.ids $(srctree)/include/linux/device.h
	sed -e '/^#/D' -e 's/^\([[:alnum:]]\{7\}\) \+"\([^"]\{1,$(DEVICE_NAME_SIZE)\}\).*"/EISA_DEVINFO ("\1", "\2"),/' $< > $@

$(src)/eisa.ids:
	touch $@
