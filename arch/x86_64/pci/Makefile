#
# Makefile for X86_64 specific PCI routines
#
# Reuse the i386 PCI subsystem using symlinks
#
obj-y		:= i386.o
obj-$(CONFIG_PCI_DIRECT)+= direct.o
obj-y		+= fixup.o
obj-$(CONFIG_ACPI_PCI)	+= acpi.o
obj-y			+= legacy.o irq.o common.o

$(obj)/direct.c: $(obj)/pci.h
	@ln -sf ../../i386/pci/direct.c $(obj)/direct.c
$(obj)/legacy.c: $(obj)/pci.h
	@ln -sf ../../i386/pci/legacy.c $(obj)/legacy.c
$(obj)/common.c: $(obj)/pci.h
	@ln -sf ../../i386/pci/common.c $(obj)/common.c
$(obj)/acpi.c: $(obj)/pci.h
	@ln -sf ../../i386/pci/acpi.c $(obj)/acpi.c
$(obj)/pci.h:
	@ln -sf ../../i386/pci/pci.h $(obj)/pci.h
$(obj)/irq.c: $(obj)/pci.h
	@ln -sf ../../i386/pci/irq.c $(obj)/irq.c
$(obj)/fixup.c: $(obj)/pci.h
	@ln -sf ../../i386/pci/fixup.c $(obj)/fixup.c
$(obj)/i386.c: $(obj)/pci.h
	@ln -sf ../../i386/pci/i386.c $(obj)/i386.c

clean-files += i386.c legacy.c fixup.c acpi.c irq.c pci.h common.c direct.c
