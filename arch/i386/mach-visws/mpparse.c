#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/bootmem.h>
#include <linux/smp_lock.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>

#include <asm/smp.h>
#include <asm/mtrr.h>
#include <asm/mpspec.h>
#include <asm/pgalloc.h>

/* Have we found an MP table */
int smp_found_config;

/*
 * Various Linux-internal data structures created from the
 * MP-table.
 */
int apic_version [MAX_APICS];
int mp_bus_id_to_type [MAX_MP_BUSSES];
int mp_bus_id_to_node [MAX_MP_BUSSES];
int mp_bus_id_to_pci_bus [MAX_MP_BUSSES] = { [0 ... MAX_MP_BUSSES-1] = -1 };
int mp_current_pci_id;

/* I/O APIC entries */
struct mpc_config_ioapic mp_ioapics[MAX_IO_APICS];

/* # of MP IRQ source entries */
struct mpc_config_intsrc mp_irqs[MAX_IRQ_SOURCES];

/* MP IRQ source entries */
int mp_irq_entries;

int nr_ioapics;

int pic_mode;
unsigned long mp_lapic_addr;

/* Processor that is doing the boot up */
unsigned int boot_cpu_physical_apicid = -1U;
unsigned int boot_cpu_logical_apicid = -1U;
/* Internal processor count */
static unsigned int num_processors;

/* Bitmask of physically existing CPUs */
unsigned long phys_cpu_present_map;

/*
 * The Visual Workstation is Intel MP compliant in the hardware
 * sense, but it doesn't have a BIOS(-configuration table).
 * No problem for Linux.
 */
void __init find_smp_config(void)
{
	smp_found_config = 1;

	phys_cpu_present_map |= 2; /* or in id 1 */
	apic_version[1] |= 0x10; /* integrated APIC */
	apic_version[0] |= 0x10;

	mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;
}

