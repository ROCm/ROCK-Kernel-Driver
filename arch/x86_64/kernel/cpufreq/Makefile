#
# Reuse the i386 cpufreq drivers
#

SRCDIR := ../../../i386/kernel/cpu/cpufreq

obj-$(CONFIG_X86_POWERNOW_K8) += powernow-k8.o
obj-$(CONFIG_X86_SPEEDSTEP_CENTRINO) += speedstep-centrino.o
obj-$(CONFIG_X86_ACPI_CPUFREQ) += acpi.o

powernow-k8-objs := ${SRCDIR}/powernow-k8.o
speedstep-centrino-objs := ${SRCDIR}/speedstep-centrino.o
acpi-objs := ${SRCDIR}/acpi.o
