/*
 * Default generic APIC driver. This handles up to 8 CPUs.
 *
 * Copyright 2003 Andi Kleen, SuSE Labs.
 * Subject to the GNU Public License, v.2
 *
 * Generic x86 APIC driver probe layer.
 */
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <asm/fixmap.h>
#include <asm/mpspec.h>
#include <asm/apicdef.h>
#include <asm/apic.h>
#include <asm/setup.h>

#include <linux/smp.h>
#include <asm/ipi.h>

#include <linux/interrupt.h>
#include <asm/acpi.h>
#include <asm/e820.h>

static int xen_phys_pkg_id(int cpuid_apic, int index_msb)
{
	return cpuid_apic;
}

static struct apic apic_xen = {

	.name				= "default",

	.irq_delivery_mode		= dest_LowestPrio,
	/* logical delivery broadcast to all CPUs: */
	.irq_dest_mode			= 1,

	.target_cpus			= default_target_cpus,

	.phys_pkg_id			= xen_phys_pkg_id,
	.mps_oem_check			= NULL,

#ifdef CONFIG_SMP
	.send_IPI_mask			= xen_send_IPI_mask,
	.send_IPI_mask_allbutself	= xen_send_IPI_mask_allbutself,
	.send_IPI_allbutself		= xen_send_IPI_allbutself,
	.send_IPI_all			= xen_send_IPI_all,
	.send_IPI_self			= xen_send_IPI_self,
#endif
};

struct apic *apic = &apic_xen;
EXPORT_SYMBOL_GPL(apic);
