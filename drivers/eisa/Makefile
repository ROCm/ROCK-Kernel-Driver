# Makefile for the Linux device tree

# Being anal sometimes saves a crash/reboot cycle... ;-)
EXTRA_CFLAGS    := -Werror

obj-$(CONFIG_EISA)	        += eisa-bus.o
obj-${CONFIG_EISA_PCI_EISA}     += pci_eisa.o

# virtual_root.o should be the last EISA root device to initialize,
# so leave it at the end of the list.
obj-${CONFIG_EISA_VIRTUAL_ROOT} += virtual_root.o

clean-files:= devlist.h

# Ugly hack to get DEVICE_NAME_SIZE value...
DEVICE_NAME_SIZE:=$(shell awk '$$1=="\#define" && $$2=="DEVICE_NAME_SIZE" {print $$3-1}' $(srctree)/include/linux/device.h)

$(obj)/eisa-bus.o: $(obj)/devlist.h

$(obj)/devlist.h: $(src)/eisa.ids $(srctree)/include/linux/device.h
	sed -e '/^#/D' -e 's/^\([[:alnum:]]\{7\}\) \+"\([^"]\{1,$(DEVICE_NAME_SIZE)\}\).*"/EISA_DEVINFO ("\1", "\2"),/' $< > $@

$(src)/eisa.ids:
	touch $@
