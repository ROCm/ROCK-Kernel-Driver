#
# arch/v850/kernel/Makefile
#
#  Copyright (C) 2001,02,03  NEC Electronics Corporation
#  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

extra-y := head.o init_task.o

obj-y += intv.o entry.o process.o syscalls.o time.o semaphore.o setup.o \
	 signal.o irq.o mach.o ptrace.o bug.o
obj-$(CONFIG_MODULES)		+= module.o v850_ksyms.o
# chip-specific code
obj-$(CONFIG_V850E_NB85E)	+= nb85e_intc.o
obj-$(CONFIG_V850E_MA1)		+= ma.o nb85e_utils.o nb85e_timer_d.o
obj-$(CONFIG_V850E_TEG)		+= teg.o nb85e_utils.o nb85e_cache.o \
				   nb85e_timer_d.o
obj-$(CONFIG_V850E2_ANNA)	+= anna.o nb85e_intc.o nb85e_utils.o \
				   nb85e_timer_d.o
obj-$(CONFIG_V850E_AS85EP1)	+= as85ep1.o nb85e_intc.o nb85e_utils.o \
				   nb85e_timer_d.o
# platform-specific code
obj-$(CONFIG_V850E_SIM)		+= sim.o simcons.o
obj-$(CONFIG_V850E2_SIM85E2C)	+= sim85e2c.o nb85e_intc.o memcons.o
obj-$(CONFIG_V850E2_FPGA85E2C)	+= fpga85e2c.o nb85e_intc.o memcons.o
obj-$(CONFIG_RTE_CB)		+= rte_cb.o rte_cb_leds.o
obj-$(CONFIG_RTE_CB_MA1)	+= rte_ma1_cb.o
obj-$(CONFIG_RTE_CB_NB85E)	+= rte_nb85e_cb.o
obj-$(CONFIG_RTE_CB_MULTI)	+= rte_cb_multi.o
obj-$(CONFIG_RTE_MB_A_PCI)	+= rte_mb_a_pci.o
obj-$(CONFIG_RTE_GBUS_INT)	+= gbus_int.o
# feature-specific code
obj-$(CONFIG_V850E_MA1_HIGHRES_TIMER)	+= highres_timer.o
obj-$(CONFIG_PROC_FS)		+= procfs.o
