obj-$(CONFIG_X86_POWERNOW_K6)	+= powernow-k6.o
obj-$(CONFIG_X86_POWERNOW_K7)	+= powernow-k7.o
obj-$(CONFIG_X86_LONGHAUL)	+= longhaul.o
obj-$(CONFIG_X86_SPEEDSTEP)	+= speedstep.o
obj-$(CONFIG_X86_P4_CLOCKMOD)	+= p4-clockmod.o
obj-$(CONFIG_ELAN_CPUFREQ)	+= elanfreq.o
obj-$(CONFIG_X86_LONGRUN)	+= longrun.o  
obj-$(CONFIG_X86_GX_SUSPMOD)    += gx-suspmod.o
obj-$(CONFIG_X86_ACPI_CPUFREQ)	+= acpi.o

ifdef CONFIG_X86_ACPI_CPUFREQ
  ifdef CONFIG_ACPI_DEBUG
    EXTRA_CFLAGS += -DACPI_DEBUG_OUTPUT
  endif
endif
