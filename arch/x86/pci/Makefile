ifeq ($(CONFIG_X86_32),y)
include ${srctree}/arch/x86/pci/Makefile_32
else
include ${srctree}/arch/x86/pci/Makefile_64
endif

# pcifront should be after mmconfig.o and direct.o as it should only
# take over if direct access to the PCI bus is unavailable
obj-$(CONFIG_XEN_PCIDEV_FRONTEND)	+= pcifront.o
