#
# Reuse the i386 cpufreq drivers
#

obj-$(CONFIG_X86_POWERNOW_K8) += powernow-k8.o

$(obj)/powernow-k8.c: $(obj)/powernow-k8.h
	@ln -sf ../../../i386/kernel/cpu/cpufreq/powernow-k8.c $(obj)/powernow-k8.c
$(obj)/powernow-k8.h:
	@ln -sf ../../../i386/kernel/cpu/cpufreq/powernow-k8.h $(obj)/powernow-k8.h

clean-files += powernow-k8.c powernow-k8.h
