#
# Makefile for the linux kernel.
#

O_TARGET	:= kernel.o
EXTRA_TARGETS	:= head.o

EXTRA_AFLAGS	:= $(CFLAGS)

export-objs := alpha_ksyms.o

obj-y    := entry.o traps.o process.o init_task.o osf_sys.o irq.o irq_alpha.o \
	    signal.o setup.o ptrace.o time.o semaphore.o alpha_ksyms.o

#
# FIXME!
# These should be made conditional on the stuff that needs them!
#
obj-y	 += irq_i8259.o irq_srm.o \
	    es1888.o smc37c669.o smc37c93x.o ns87312.o

ifdef CONFIG_VGA_HOSE
obj-y	 += console.o
endif

obj-$(CONFIG_SMP)    += smp.o irq_smp.o
obj-$(CONFIG_PCI)    += pci.o pci_iommu.o
obj-$(CONFIG_SRM_ENV)	+= srm_env.o

ifdef CONFIG_ALPHA_GENERIC

obj-y 	 += core_apecs.o core_cia.o core_irongate.o core_lca.o core_mcpcia.o \
	    core_polaris.o core_t2.o core_tsunami.o core_titan.o \
	    sys_alcor.o sys_cabriolet.o sys_dp264.o sys_eb64p.o sys_eiger.o \
	    sys_jensen.o sys_miata.o sys_mikasa.o sys_nautilus.o sys_titan.o \
	    sys_noritake.o sys_rawhide.o sys_ruffian.o sys_rx164.o \
	    sys_sable.o sys_sio.o sys_sx164.o sys_takara.o sys_rx164.o \
	    sys_wildfire.o core_wildfire.o irq_pyxis.o

else

# Core logic support
obj-$(CONFIG_ALPHA_APECS) += core_apecs.o
obj-$(CONFIG_ALPHA_CIA) += core_cia.o

obj-$(CONFIG_ALPHA_IRONGATE) += core_irongate.o
obj-$(CONFIG_ALPHA_LCA) += core_lca.o
obj-$(CONFIG_ALPHA_MCPCIA) += core_mcpcia.o
obj-$(CONFIG_ALPHA_T2) += core_t2.o
obj-$(CONFIG_ALPHA_TSUNAMI) += core_tsunami.o
obj-$(CONFIG_ALPHA_TITAN) += core_titan.o
obj-$(CONFIG_ALPHA_POLARIS) += core_polaris.o
obj-$(CONFIG_ALPHA_WILDFIRE) += core_wildfire.o

# Board support
ifneq ($(CONFIG_ALPHA_ALCOR)$(CONFIG_ALPHA_XLT),)
obj-y    += sys_alcor.o
endif
ifneq ($(CONFIG_ALPHA_CABRIOLET)$(CONFIG_ALPHA_EB164)$(CONFIG_ALPHA_EB66P)$(CONFIG_ALPHA_LX164)$(CONFIG_ALPHA_PC164),)
obj-y    += sys_cabriolet.o
endif

obj-$(CONFIG_ALPHA_DP264) += sys_dp264.o
obj-$(CONFIG_ALPHA_SHARK) += sys_dp264.o
obj-$(CONFIG_ALPHA_TITAN) += sys_titan.o

ifneq ($(CONFIG_ALPHA_EB64P)$(CONFIG_ALPHA_EB66),)
obj-y    += sys_eb64p.o
endif

obj-$(CONFIG_ALPHA_EIGER) += sys_eiger.o
obj-$(CONFIG_ALPHA_JENSEN) += sys_jensen.o pci-noop.o
obj-$(CONFIG_ALPHA_MIATA) += sys_miata.o
obj-$(CONFIG_ALPHA_MIKASA) += sys_mikasa.o
obj-$(CONFIG_ALPHA_NAUTILUS) += sys_nautilus.o
obj-$(CONFIG_ALPHA_NORITAKE) += sys_noritake.o
obj-$(CONFIG_ALPHA_RAWHIDE) += sys_rawhide.o
obj-$(CONFIG_ALPHA_RUFFIAN) += sys_ruffian.o
obj-$(CONFIG_ALPHA_RX164) += sys_rx164.o
obj-$(CONFIG_ALPHA_SABLE) += sys_sable.o

ifneq ($(CONFIG_ALPHA_BOOK1)$(CONFIG_ALPHA_AVANTI)$(CONFIG_ALPHA_NONAME)$(CONFIG_ALPHA_P2K)$(CONFIG_ALPHA_XL),)
obj-y    += sys_sio.o
endif

obj-$(CONFIG_ALPHA_SX164) += sys_sx164.o
obj-$(CONFIG_ALPHA_TAKARA) += sys_takara.o
obj-$(CONFIG_ALPHA_WILDFIRE) += sys_wildfire.o

ifneq ($(CONFIG_ALPHA_MIATA)$(CONFIG_ALPHA_RUFFIAN)$(CONFIG_ALPHA_SX164),)
obj-y    += irq_pyxis.o
endif

endif # GENERIC

include $(TOPDIR)/Rules.make

ASM_OFFSETS_H = $(TOPDIR)/include/asm-alpha/asm_offsets.h
asm_offsets:
	$(CC) $(CFLAGS) -S -o - check_asm.c | \
	sed -e '/xyzzy/ { s/xyzzy //; p; }; d;' > asm_offsets.tmp
	@if cmp -s asm_offsets.tmp $(ASM_OFFSETS_H); then \
	  set -x; rm asm_offsets.tmp; \
	else \
	  set -x; mv asm_offsets.tmp $(ASM_OFFSETS_H); \
	fi

clean::
	rm -f check_asm

