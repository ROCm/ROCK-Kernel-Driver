/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Generic APIC sub-arch probe layer.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 */
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/hardirq.h>

#include <asm/smp.h>
#include <asm/genapic.h>
#include <asm/setup.h>

#ifndef CONFIG_XEN
extern struct genapic apic_flat;
extern struct genapic apic_physflat;
extern struct genapic apic_x2xpic_uv_x;
extern struct genapic apic_x2apic_phys;
extern struct genapic apic_x2apic_cluster;

struct genapic __read_mostly *genapic = &apic_flat;

static struct genapic *apic_probe[] __initdata = {
	&apic_x2apic_uv_x,
	&apic_x2apic_phys,
	&apic_x2apic_cluster,
	&apic_physflat,
	NULL,
};
#else
extern struct genapic apic_xen;
struct genapic __read_mostly *genapic = &apic_xen;
#endif


/*
 * Check the APIC IDs in bios_cpu_apicid and choose the APIC mode.
 */
void __init setup_apic_routing(void)
{
#ifndef CONFIG_XEN
	if (genapic == &apic_x2apic_phys || genapic == &apic_x2apic_cluster) {
		if (!intr_remapping_enabled)
			genapic = &apic_flat;
	}

	if (genapic == &apic_flat) {
		if (max_physical_apicid >= 8)
			genapic = &apic_physflat;
		printk(KERN_INFO "Setting APIC routing to %s\n", genapic->name);
	}

	if (x86_quirks->update_genapic)
		x86_quirks->update_genapic();
#else
	/* hardcode to xen apic functions */
	genapic = &apic_xen;
#endif
}

/* Same for both flat and physical. */

void apic_send_IPI_self(int vector)
{
#ifndef CONFIG_XEN
	__send_IPI_shortcut(APIC_DEST_SELF, vector, APIC_DEST_PHYSICAL);
#else
	xen_send_IPI_shortcut(APIC_DEST_SELF, vector);
#endif
}

int __init acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
#ifndef CONFIG_XEN
	int i;

	for (i = 0; apic_probe[i]; ++i) {
		if (apic_probe[i]->acpi_madt_oem_check(oem_id, oem_table_id)) {
			genapic = apic_probe[i];
			printk(KERN_INFO "Setting APIC routing to %s.\n",
				genapic->name);
			return 1;
		}
	}
#endif
	return 0;
}
