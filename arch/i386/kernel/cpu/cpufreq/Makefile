obj-$(CONFIG_X86_POWERNOW_K6)	+= powernow-k6.o
obj-$(CONFIG_X86_POWERNOW_K7)	+= powernow-k7.o
obj-$(CONFIG_X86_POWERNOW_K8)	+= powernow-k8.o
obj-$(CONFIG_X86_LONGHAUL)	+= longhaul.o
obj-$(CONFIG_X86_P4_CLOCKMOD)	+= p4-clockmod.o
obj-$(CONFIG_ELAN_CPUFREQ)	+= elanfreq.o
obj-$(CONFIG_X86_LONGRUN)	+= longrun.o  
obj-$(CONFIG_X86_GX_SUSPMOD)    += gx-suspmod.o
obj-$(CONFIG_X86_ACPI_CPUFREQ)	+= acpi.o
obj-$(CONFIG_X86_SPEEDSTEP_ICH)	+= speedstep-ich.o
obj-$(CONFIG_X86_SPEEDSTEP_CENTRINO) += speedstep-centrino.o
obj-$(CONFIG_X86_SPEEDSTEP_LIB) += speedstep-lib.o
obj-$(CONFIG_X86_SPEEDSTEP_SMI) += speedstep-smi.o

ifdef CONFIG_X86_ACPI_CPUFREQ
  ifdef CONFIG_ACPI_DEBUG
    EXTRA_CFLAGS += -DACPI_DEBUG_OUTPUT
  endif
endif
