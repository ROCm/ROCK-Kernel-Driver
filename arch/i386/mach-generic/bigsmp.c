/* 
 * APIC driver for "bigsmp" XAPIC machines with more than 8 virtual CPUs.
 * Drives the local APIC in "clustered mode".
 */
#define APIC_DEFINITION 1
#include <linux/config.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/genapic.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <asm/mach-bigsmp/mach_apic.h>
#include <asm/mach-bigsmp/mach_apicdef.h>
#include <asm/mach-bigsmp/mach_ipi.h>
#include <asm/mach-default/mach_mpparse.h>

int dmi_bigsmp; /* can be set by dmi scanners */

static __init int probe_bigsmp(void)
{ 
	return dmi_bigsmp; 
} 

struct genapic apic_bigsmp = APIC_INIT("bigsmp", probe_bigsmp); 
