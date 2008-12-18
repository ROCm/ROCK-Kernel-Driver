/*
 * APIC driver for "bigsmp" XAPIC machines with more than 8 virtual CPUs.
 * Drives the local APIC in "clustered mode".
 */
#define APIC_DEFINITION 1
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/genapic.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <asm/mach-bigsmp/mach_apicdef.h>
#include <linux/smp.h>
#include <asm/mach-bigsmp/mach_apic.h>
#include <asm/mach-bigsmp/mach_ipi.h>
#include <asm/mach-default/mach_mpparse.h>

static int dmi_bigsmp; /* can be set by dmi scanners */

static int force_bigsmp_apic(const struct dmi_system_id *d)
{
	printk(KERN_NOTICE "%s detected: force use of apic=bigsmp\n", d->ident);
	dmi_bigsmp = 1;
	return 0;
}


static const struct dmi_system_id bigsmp_dmi_table[] = {
	{ force_bigsmp_apic, "HP ProLiant DL760 G2",
	{ DMI_MATCH(DMI_BIOS_VENDOR, "HP"),
	DMI_MATCH(DMI_BIOS_VERSION, "P44-"),}
	},

	{ force_bigsmp_apic, "HP ProLiant DL740",
	{ DMI_MATCH(DMI_BIOS_VENDOR, "HP"),
	DMI_MATCH(DMI_BIOS_VERSION, "P47-"),}
	},

	{ force_bigsmp_apic, "IBM x260 / x366 / x460",
	{ DMI_MATCH(DMI_BIOS_VENDOR, "IBM"),
	DMI_MATCH(DMI_BIOS_VERSION, "-[ZT"),}
	},

	{ force_bigsmp_apic, "IBM x3800 / x3850 / x3950",
	{ DMI_MATCH(DMI_BIOS_VENDOR, "IBM"),
	DMI_MATCH(DMI_BIOS_VERSION, "-[ZU"),}
	},

	{ force_bigsmp_apic, "IBM x3800 / x3850 / x3950",
	{ DMI_MATCH(DMI_BIOS_VENDOR, "IBM"),
	DMI_MATCH(DMI_BIOS_VERSION, "-[ZS"),}
	},

	{ force_bigsmp_apic, "IBM x3850 M2 / x3950 M2",
	{ DMI_MATCH(DMI_BIOS_VENDOR, "IBM"),
	DMI_MATCH(DMI_BIOS_VERSION, "-[A3"),}
	},
	 { }
};


static int probe_bigsmp(void)
{
	if (def_to_bigsmp)
		dmi_bigsmp = 1;
	else
		dmi_check_system(bigsmp_dmi_table);
	return dmi_bigsmp;
}

struct genapic apic_bigsmp = APIC_INIT("bigsmp", probe_bigsmp);
