#
# Makefile for x86-compatible CPU details and quirks
#

obj-y	:=	common.o proc.o bugs.o

obj-y	+=	amd.o
obj-y	+=	cyrix.o
obj-y	+=	centaur.o
obj-y	+=	transmeta.o
obj-y	+=	intel.o intel_cacheinfo.o
obj-y	+=	rise.o
obj-y	+=	nexgen.o
obj-y	+=	umc.o

obj-$(CONFIG_X86_MCE)	+=	mcheck/

obj-$(CONFIG_MTRR)	+= 	mtrr/
obj-$(CONFIG_CPU_FREQ)	+=	cpufreq/

obj-$(CONFIG_X86_LOCAL_APIC) += perfctr-watchdog.o

ifdef CONFIG_XEN
include $(srctree)/scripts/Makefile.xen
n-obj-xen := perfctr-watchdog.o
obj-y := $(call filterxen, $(obj-y), $(n-obj-xen))
obj-y := $(call cherrypickxen, $(obj-y), $(src))
endif
