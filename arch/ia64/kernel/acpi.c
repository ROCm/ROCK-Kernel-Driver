/*
 *  acpi.c - Architecture-Specific Low-Level ACPI Support
 *
 *  Copyright (C) 1999 VA Linux Systems
 *  Copyright (C) 1999,2000 Walt Drummond <drummond@valinux.com>
 *  Copyright (C) 2000, 2002 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *  Copyright (C) 2000 Intel Corp.
 *  Copyright (C) 2000,2001 J.I. Lee <jung-ik.lee@intel.com>
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/acpi.h>
#include <asm/efi.h>
#include <asm/io.h>
#include <asm/iosapic.h>
#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/system.h>


#define PREFIX			"ACPI: "

asm (".weak iosapic_register_irq");
asm (".weak iosapic_register_legacy_irq");
asm (".weak iosapic_register_platform_irq");
asm (".weak iosapic_init");
asm (".weak iosapic_version");

void (*pm_idle) (void);
void (*pm_power_off) (void);

unsigned char acpi_kbd_controller_present = 1;

const char *
acpi_get_sysname (void)
{
#ifdef CONFIG_IA64_GENERIC
	unsigned long rsdp_phys;
	struct acpi20_table_rsdp *rsdp;
	struct acpi_table_xsdt *xsdt;
	struct acpi_table_header *hdr;

	rsdp_phys = acpi_find_rsdp();
	if (!rsdp_phys) {
		printk("ACPI 2.0 RSDP not found, default to \"dig\"\n");
		return "dig";
	}

	rsdp = (struct acpi20_table_rsdp *) __va(rsdp_phys);
	if (strncmp(rsdp->signature, RSDP_SIG, sizeof(RSDP_SIG) - 1)) {
		printk("ACPI 2.0 RSDP signature incorrect, default to \"dig\"\n");
		return "dig";
	}

	xsdt = (struct acpi_table_xsdt *) __va(rsdp->xsdt_address);
	hdr = &xsdt->header;
	if (strncmp(hdr->signature, XSDT_SIG, sizeof(XSDT_SIG) - 1)) {
		printk("ACPI 2.0 XSDT signature incorrect, default to \"dig\"\n");
		return "dig";
	}

	if (!strcmp(hdr->oem_id, "HP")) {
		return "hpzx1";
	}

	return "dig";
#else
# if defined (CONFIG_IA64_HP_SIM)
	return "hpsim";
# elif defined (CONFIG_IA64_HP_ZX1)
	return "hpzx1";
# elif defined (CONFIG_IA64_SGI_SN1)
	return "sn1";
# elif defined (CONFIG_IA64_SGI_SN2)
	return "sn2";
# elif defined (CONFIG_IA64_DIG)
	return "dig";
# elif defined (CONFIG_IA64_HP_ZX1)
	return "hpzx1";
# else
#	error Unknown platform.  Fix acpi.c.
# endif
#endif
}

#ifdef CONFIG_ACPI

/**
 * acpi_get_crs - Return the current resource settings for a device
 * obj: A handle for this device
 * buf: A buffer to be populated by this call.
 *
 * Pass a valid handle, typically obtained by walking the namespace and a
 * pointer to an allocated buffer, and this function will fill in the buffer
 * with a list of acpi_resource structures.
 */
acpi_status
acpi_get_crs (acpi_handle obj, acpi_buffer *buf)
{
	acpi_status result;
	buf->length = 0;
	buf->pointer = NULL;

	result = acpi_get_current_resources(obj, buf);
	if (result != AE_BUFFER_OVERFLOW)
		return result;
	buf->pointer = kmalloc(buf->length, GFP_KERNEL);
	if (!buf->pointer)
		return -ENOMEM;

	return acpi_get_current_resources(obj, buf);
}

acpi_resource *
acpi_get_crs_next (acpi_buffer *buf, int *offset)
{
	acpi_resource *res;

	if (*offset >= buf->length)
		return NULL;

	res = buf->pointer + *offset;
	*offset += res->length;
	return res;
}

acpi_resource_data *
acpi_get_crs_type (acpi_buffer *buf, int *offset, int type)
{
	for (;;) {
		acpi_resource *res = acpi_get_crs_next(buf, offset);
		if (!res)
			return NULL;
		if (res->id == type)
			return &res->data;
	}
}

void
acpi_dispose_crs (acpi_buffer *buf)
{
	kfree(buf->pointer);
}

#endif /* CONFIG_ACPI */

#ifdef CONFIG_ACPI_BOOT

#define ACPI_MAX_PLATFORM_IRQS	256

/* Array to record platform interrupt vectors for generic interrupt routing. */
int platform_irq_list[ACPI_MAX_PLATFORM_IRQS] = { [0 ... ACPI_MAX_PLATFORM_IRQS - 1] = -1 };

enum acpi_irq_model_id acpi_irq_model = ACPI_IRQ_MODEL_IOSAPIC;

/*
 * Interrupt routing API for device drivers.  Provides interrupt vector for
 * a generic platform event.  Currently only CPEI is implemented.
 */
int
acpi_request_vector (u32 int_type)
{
	int vector = -1;

	if (int_type < ACPI_MAX_PLATFORM_IRQS) {
		/* correctable platform error interrupt */
		vector = platform_irq_list[int_type];
	} else
		printk("acpi_request_vector(): invalid interrupt type\n");
	return vector;
}

char *
__acpi_map_table (unsigned long phys_addr, unsigned long size)
{
	return __va(phys_addr);
}

/* --------------------------------------------------------------------------
                            Boot-time Table Parsing
   -------------------------------------------------------------------------- */

static int			total_cpus __initdata;
static int			available_cpus __initdata;
struct acpi_table_madt *	acpi_madt __initdata;


static int __init
acpi_parse_lapic_addr_ovr (acpi_table_entry_header *header)
{
	struct acpi_table_lapic_addr_ovr *lapic;

	lapic = (struct acpi_table_lapic_addr_ovr *) header;
	if (!lapic)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (lapic->address) {
		iounmap((void *) ipi_base_addr);
		ipi_base_addr = (unsigned long) ioremap(lapic->address, 0);
	}
	return 0;
}


static int __init
acpi_parse_lsapic (acpi_table_entry_header *header)
{
	struct acpi_table_lsapic *lsapic;

	lsapic = (struct acpi_table_lsapic *) header;
	if (!lsapic)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	printk("CPU %d (0x%04x)", total_cpus, (lsapic->id << 8) | lsapic->eid);

	if (lsapic->flags.enabled) {
		available_cpus++;
		printk(" enabled");
#ifdef CONFIG_SMP
		smp_boot_data.cpu_phys_id[total_cpus] = (lsapic->id << 8) | lsapic->eid;
		if (hard_smp_processor_id() == smp_boot_data.cpu_phys_id[total_cpus])
			printk(" (BSP)");
#endif
	}
	else {
		printk(" disabled");
#ifdef CONFIG_SMP
		smp_boot_data.cpu_phys_id[total_cpus] = -1;
#endif
	}

	printk("\n");

	total_cpus++;
	return 0;
}


static int __init
acpi_parse_lapic_nmi (acpi_table_entry_header *header)
{
	struct acpi_table_lapic_nmi *lacpi_nmi;

	lacpi_nmi = (struct acpi_table_lapic_nmi*) header;
	if (!lacpi_nmi)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TBD: Support lapic_nmi entries */
	return 0;
}


static int __init
acpi_find_iosapic (int global_vector, u32 *irq_base, char **iosapic_address)
{
	struct acpi_table_iosapic *iosapic;
	int ver;
	int max_pin;
	char *p;
	char *end;

	if (!irq_base || !iosapic_address)
		return -ENODEV;

	p = (char *) (acpi_madt + 1);
	end = p + (acpi_madt->header.length - sizeof(struct acpi_table_madt));

	while (p < end) {
		if (*p == ACPI_MADT_IOSAPIC) {
			iosapic = (struct acpi_table_iosapic *) p;

			*irq_base = iosapic->global_irq_base;
			*iosapic_address = ioremap(iosapic->address, 0);

			ver = iosapic_version(*iosapic_address);
			max_pin = (ver >> 16) & 0xff;

			if ((global_vector - *irq_base) <= max_pin)
				return 0;	/* Found it! */
		}
		p += p[1];
	}
	return -ENODEV;
}


static int __init
acpi_parse_iosapic (acpi_table_entry_header *header)
{
	struct acpi_table_iosapic *iosapic;

	iosapic = (struct acpi_table_iosapic *) header;
	if (!iosapic)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (iosapic_init) {
#ifndef CONFIG_ITANIUM
		/* PCAT_COMPAT flag indicates dual-8259 setup */
		iosapic_init(iosapic->address, iosapic->global_irq_base,
			     acpi_madt->flags.pcat_compat);
#else
		/* Firmware on old Itanium systems is broken */
		iosapic_init(iosapic->address, iosapic->global_irq_base, 1);
#endif
	}
	return 0;
}


static int __init
acpi_parse_plat_int_src (acpi_table_entry_header *header)
{
	struct acpi_table_plat_int_src *plintsrc;
	int vector;
	u32 irq_base;
	char *iosapic_address;

	plintsrc = (struct acpi_table_plat_int_src *) header;
	if (!plintsrc)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (!iosapic_register_platform_irq) {
		printk(KERN_WARNING PREFIX "No ACPI platform IRQ support\n");
		return -ENODEV;
	}

	if (acpi_find_iosapic(plintsrc->global_irq, &irq_base, &iosapic_address)) {
		printk(KERN_WARNING PREFIX "IOSAPIC not found\n");
		return -ENODEV;
	}

	/*
	 * Get vector assignment for this IRQ, set attributes, and program the
	 * IOSAPIC routing table.
	 */
	vector = iosapic_register_platform_irq(plintsrc->type,
					       plintsrc->global_irq,
					       plintsrc->iosapic_vector,
					       plintsrc->eid,
					       plintsrc->id,
					       (plintsrc->flags.polarity == 1) ? 1 : 0,
					       (plintsrc->flags.trigger == 1) ? 1 : 0,
					       irq_base,
					       iosapic_address);

	platform_irq_list[plintsrc->type] = vector;
	return 0;
}


static int __init
acpi_parse_int_src_ovr (acpi_table_entry_header *header)
{
	struct acpi_table_int_src_ovr *p;

	p = (struct acpi_table_int_src_ovr *) header;
	if (!p)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* Ignore if the platform doesn't support overrides */
	if (!iosapic_register_legacy_irq)
		return 0;

	iosapic_register_legacy_irq(p->bus_irq, p->global_irq,
				    (p->flags.polarity == 1) ? 1 : 0,
				    (p->flags.trigger == 1) ? 1 : 0);
	return 0;
}


static int __init
acpi_parse_nmi_src (acpi_table_entry_header *header)
{
	struct acpi_table_nmi_src *nmi_src;

	nmi_src = (struct acpi_table_nmi_src*) header;
	if (!nmi_src)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TBD: Support nimsrc entries */
	return 0;
}


static int __init
acpi_parse_madt (unsigned long phys_addr, unsigned long size)
{
	if (!phys_addr || !size)
		return -EINVAL;

	acpi_madt = (struct acpi_table_madt *) __va(phys_addr);

	/* Get base address of IPI Message Block */

	if (acpi_madt->lapic_address)
		ipi_base_addr = (unsigned long) ioremap(acpi_madt->lapic_address, 0);

	printk(KERN_INFO PREFIX "Local APIC address 0x%lx\n", ipi_base_addr);
	return 0;
}

static int __init
acpi_parse_fadt (unsigned long phys_addr, unsigned long size)
{
	struct acpi_table_header *fadt_header;
	fadt_descriptor_rev2 *fadt;

	if (!phys_addr || !size)
		return -EINVAL;

	fadt_header = (struct acpi_table_header *) __va(phys_addr);
	if (fadt_header->revision != 3)
		return -ENODEV;		/* Only deal with ACPI 2.0 FADT */

	fadt = (fadt_descriptor_rev2 *) fadt_header;

	if (!(fadt->iapc_boot_arch & BAF_8042_KEYBOARD_CONTROLLER))
		acpi_kbd_controller_present = 0;

	return 0;
}

unsigned long __init
acpi_find_rsdp (void)
{
	unsigned long rsdp_phys = 0;

	if (efi.acpi20)
		rsdp_phys = __pa(efi.acpi20);
	else if (efi.acpi)
		printk(KERN_WARNING PREFIX "v1.0/r0.71 tables no longer supported\n");
	return rsdp_phys;
}


#ifdef CONFIG_SERIAL_8250_ACPI

#include <linux/acpi_serial.h>

static int __init
acpi_parse_spcr (unsigned long phys_addr, unsigned long size)
{
	acpi_ser_t *spcr;
	unsigned long global_int;

	if (!phys_addr || !size)
		return -EINVAL;

	if (!iosapic_register_irq)
		return -ENODEV;

	/*
	 * ACPI is able to describe serial ports that live at non-standard
	 * memory addresses and use non-standard interrupts, either via
	 * direct SAPIC mappings or via PCI interrupts.  We handle interrupt
	 * routing for SAPIC-based (non-PCI) devices here.  Interrupt routing
	 * for PCI devices will be handled when processing the PCI Interrupt
	 * Routing Table (PRT).
	 */

	spcr = (acpi_ser_t *) __va(phys_addr);
	setup_serial_acpi(spcr);

	if (spcr->length < sizeof(acpi_ser_t))
		/* Table not long enough for full info, thus no interrupt */
		return -ENODEV;

	if ((spcr->base_addr.space_id != ACPI_SERIAL_PCICONF_SPACE) &&
	    (spcr->int_type == ACPI_SERIAL_INT_SAPIC))
	{
		u32 irq_base;
		char *iosapic_address;
		int vector;

		/* We have a UART in memory space with an SAPIC interrupt */

		global_int = ((spcr->global_int[3] << 24) |
			      (spcr->global_int[2] << 16) |
			      (spcr->global_int[1] << 8)  |
			      (spcr->global_int[0])  );

		/* Which iosapic does this IRQ belong to? */

		if (!acpi_find_iosapic(global_int, &irq_base, &iosapic_address))
			vector = iosapic_register_irq(global_int, 1, 1,
						      irq_base, iosapic_address);
	}
	return 0;
}

#endif /* CONFIG_SERIAL_8250_ACPI */


int __init
acpi_boot_init (char *cmdline)
{
	int result;

	/* Initialize the ACPI boot-time table parser */
	result = acpi_table_init(cmdline);
	if (result)
		return result;

	/*
	 * MADT
	 * ----
	 * Parse the Multiple APIC Description Table (MADT), if exists.
	 * Note that this table provides platform SMP configuration
	 * information -- the successor to MPS tables.
	 */

	if (acpi_table_parse(ACPI_APIC, acpi_parse_madt) < 1) {
		printk(KERN_ERR PREFIX "Can't find MADT\n");
		goto skip_madt;
	}

	/* Local APIC */

	if (acpi_table_parse_madt(ACPI_MADT_LAPIC_ADDR_OVR, acpi_parse_lapic_addr_ovr) < 0)
		printk(KERN_ERR PREFIX "Error parsing LAPIC address override entry\n");

	if (acpi_table_parse_madt(ACPI_MADT_LSAPIC, acpi_parse_lsapic) < 1)
		printk(KERN_ERR PREFIX "Error parsing MADT - no LAPIC entries\n");

	if (acpi_table_parse_madt(ACPI_MADT_LAPIC_NMI, acpi_parse_lapic_nmi) < 0)
		printk(KERN_ERR PREFIX "Error parsing LAPIC NMI entry\n");

	/* I/O APIC */

	if (acpi_table_parse_madt(ACPI_MADT_IOSAPIC, acpi_parse_iosapic) < 1)
		printk(KERN_ERR PREFIX "Error parsing MADT - no IOSAPIC entries\n");

	/* System-Level Interrupt Routing */

	if (acpi_table_parse_madt(ACPI_MADT_PLAT_INT_SRC, acpi_parse_plat_int_src) < 0)
		printk(KERN_ERR PREFIX "Error parsing platform interrupt source entry\n");

	if (acpi_table_parse_madt(ACPI_MADT_INT_SRC_OVR, acpi_parse_int_src_ovr) < 0)
		printk(KERN_ERR PREFIX "Error parsing interrupt source overrides entry\n");

	if (acpi_table_parse_madt(ACPI_MADT_NMI_SRC, acpi_parse_nmi_src) < 0)
		printk(KERN_ERR PREFIX "Error parsing NMI SRC entry\n");
  skip_madt:

	/* FADT says whether a legacy keyboard controller is present. */
	if (acpi_table_parse(ACPI_FACP, acpi_parse_fadt) < 1)
		printk(KERN_ERR PREFIX "Can't find FADT\n");

#ifdef CONFIG_SERIAL_8250_ACPI
	/*
	 * TBD: Need phased approach to table parsing (only do those absolutely
	 *      required during boot-up).  Recommend expanding concept of fix-
	 *      feature devices (LDM) to include table-based devices such as
	 *      serial ports, EC, SMBus, etc.
	 */
	acpi_table_parse(ACPI_SPCR, acpi_parse_spcr);
#endif

#ifdef CONFIG_SMP
	if (available_cpus == 0) {
		printk("ACPI: Found 0 CPUS; assuming 1\n");
		available_cpus = 1; /* We've got at least one of these, no? */
	}
	smp_boot_data.cpu_count = total_cpus;
#endif
	/* Make boot-up look pretty */
	printk("%d CPUs available, %d CPUs total\n", available_cpus, total_cpus);
	return 0;
}


/* --------------------------------------------------------------------------
                             PCI Interrupt Routing
   -------------------------------------------------------------------------- */

int __init
acpi_get_prt (struct pci_vector_struct **vectors, int *count)
{
	struct pci_vector_struct *vector;
	struct list_head *node;
	struct acpi_prt_entry *entry;
	int i = 0;

	if (!vectors || !count)
		return -EINVAL;

	*vectors = NULL;
	*count = 0;

	if (acpi_prt.count < 0) {
		printk(KERN_ERR PREFIX "No PCI IRQ routing entries\n");
		return -ENODEV;
	}

	/* Allocate vectors */

	*vectors = kmalloc(sizeof(struct pci_vector_struct) * acpi_prt.count, GFP_KERNEL);
	if (!(*vectors))
		return -ENOMEM;

	/* Convert PRT entries to IOSAPIC PCI vectors */

	vector = *vectors;

	list_for_each(node, &acpi_prt.entries) {
		entry = (struct acpi_prt_entry *)node;
		vector[i].bus    = entry->id.bus;
		vector[i].pci_id = ((u32) entry->id.device << 16) | 0xffff;
		vector[i].pin    = entry->pin;
		vector[i].irq    = entry->link.index;
		i++;
	}
	*count = acpi_prt.count;
	return 0;
}

/* Assume IA64 always use I/O SAPIC */

int __init
acpi_get_interrupt_model (int *type)
{
        if (!type)
                return -EINVAL;

	*type = ACPI_IRQ_MODEL_IOSAPIC;
        return 0;
}

#endif /* CONFIG_ACPI_BOOT */
