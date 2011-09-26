pcibk-$(CONFIG_PARAVIRT_XEN) := xen-pciback
pcibk-$(CONFIG_XEN) := pciback

obj-$(CONFIG_XEN_PCIDEV_BACKEND) := $(pcibk-y).o

$(pcibk-y)-y := pci_stub.o pciback_ops.o xenbus.o
$(pcibk-y)-y += conf_space.o conf_space_header.o \
		 conf_space_capability.o \
		 conf_space_quirks.o
$(pcibk-y)-$(CONFIG_XEN_PCIDEV_BACKEND_VPCI) += vpci.o
$(pcibk-y)-$(CONFIG_XEN_PCIDEV_BACKEND_PASSTHROUGH) += passthrough.o
$(pcibk-y)-$(CONFIG_XEN_PCIDEV_BACKEND_SLOT) += slot.o
$(pcibk-y)-$(CONFIG_XEN_PCIDEV_BACKEND_CONTROLLER) += controller.o
