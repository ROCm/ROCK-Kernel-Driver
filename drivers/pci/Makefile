#
# Makefile for the PCI bus specific drivers.
#

obj-y		+= access.o bus.o probe.o pci.o pool.o quirks.o \
			names.o pci-driver.o search.o hotplug.o \
			pci-sysfs.o
obj-$(CONFIG_PM)  += power.o
obj-$(CONFIG_PROC_FS) += proc.o

ifndef CONFIG_SPARC64
obj-$(CONFIG_PCI) += setup-res.o
endif

#
# Some architectures use the generic PCI setup functions
#
obj-$(CONFIG_ALPHA) += setup-bus.o setup-irq.o
obj-$(CONFIG_ARM) += setup-bus.o setup-irq.o
obj-$(CONFIG_PARISC) += setup-bus.o
obj-$(CONFIG_SUPERH) += setup-bus.o setup-irq.o
obj-$(CONFIG_PPC32) += setup-irq.o
obj-$(CONFIG_PPC64) += setup-bus.o
obj-$(CONFIG_DDB5476) += setup-bus.o
obj-$(CONFIG_SGI_IP27) += setup-irq.o
obj-$(CONFIG_X86_VISWS) += setup-irq.o

# CompactPCI hotplug requires the pbus_* functions
ifdef CONFIG_HOTPLUG_PCI_CPCI
obj-y += setup-bus.o
endif

# Hotplug (eg, cardbus) now requires setup-bus
obj-$(CONFIG_HOTPLUG) += setup-bus.o

ifndef CONFIG_X86
obj-y += syscall.o
endif

host-progs := gen-devlist

# Dependencies on generated files need to be listed explicitly
$(obj)/names.o: $(obj)/devlist.h $(obj)/classlist.h
$(obj)/classlist.h: $(obj)/devlist.h

# And that's how to generate them
quiet_cmd_devlist = DEVLIST $@
      cmd_devlist = ( cd $(obj); ./gen-devlist ) < $<
$(obj)/devlist.h: $(src)/pci.ids $(obj)/gen-devlist
	$(call cmd,devlist)

# Files generated that shall be removed upon make clean
clean-files := devlist.h classlist.h


