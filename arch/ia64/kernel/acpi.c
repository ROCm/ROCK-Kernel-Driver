/*
 * Advanced Configuration and Power Interface 
 *
 * Based on 'ACPI Specification 1.0b' February 2, 1999 and 
 * 'IA-64 Extensions to ACPI Specification' Revision 0.6
 * 
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999,2000 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 2000 Hewlett-Packard Co.
 * Copyright (C) 2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000 Intel Corp.
 * Copyright (C) 2000 J.I. Lee <jung-ik.lee@intel.com>
 *      ACPI based kernel configuration manager.
 *      ACPI 2.0 & IA64 ext 0.71
 */

#include <linux/config.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/irq.h>

#include <asm/acpi-ext.h>
#include <asm/efi.h>
#include <asm/io.h>
#include <asm/iosapic.h>
#include <asm/machvec.h>
#include <asm/page.h>
#ifdef CONFIG_ACPI_KERNEL_CONFIG
# include <asm/acpikcfg.h>
#endif

#undef ACPI_DEBUG		/* Guess what this does? */

/* These are ugly but will be reclaimed by the kernel */
int __initdata available_cpus;
int __initdata total_cpus;

void (*pm_idle)(void);

asm (".weak iosapic_register_legacy_irq");
asm (".weak iosapic_init");

const char *
acpi_get_sysname (void)
{
	/* the following should go away once we have an ACPI parser: */
#ifdef CONFIG_IA64_GENERIC
	return "hpsim";
#else
# if defined (CONFIG_IA64_HP_SIM)
	return "hpsim";
# elif defined (CONFIG_IA64_SGI_SN1)
	return "sn1";
# elif defined (CONFIG_IA64_DIG)
	return "dig";
# else
#	error Unknown platform.  Fix acpi.c.
# endif
#endif

}

/*
 * Configure legacy IRQ information.
 */
static void __init
acpi_legacy_irq (char *p)
{
	acpi_entry_int_override_t *legacy = (acpi_entry_int_override_t *) p;
	unsigned long polarity = 0, edge_triggered = 0;

	/*
	 * If the platform we're running doesn't define
	 * iosapic_register_legacy_irq(), we ignore this info...
	 */
	if (!iosapic_register_legacy_irq)
		return;

	switch (legacy->flags) {
	      case 0x5:	polarity = 1; edge_triggered = 1; break;
	      case 0x7: polarity = 0; edge_triggered = 1; break;
	      case 0xd: polarity = 1; edge_triggered = 0; break;
	      case 0xf: polarity = 0; edge_triggered = 0; break;
	      default:
		printk("    ACPI Legacy IRQ 0x%02x: Unknown flags 0x%x\n", legacy->isa_irq,
		       legacy->flags);
		break;
	}
	iosapic_register_legacy_irq(legacy->isa_irq, legacy->pin, polarity, edge_triggered);
}

/*
 * ACPI 2.0 tables parsing functions
 */

static unsigned long
readl_unaligned(void *p)
{
	unsigned long ret;

	memcpy(&ret, p, sizeof(long));
	return ret;
}

/*
 * Identify usable CPU's and remember them for SMP bringup later.
 */
static void __init
acpi20_lsapic (char *p) 
{
	int add = 1;

	acpi20_entry_lsapic_t *lsapic = (acpi20_entry_lsapic_t *) p;
	printk("      CPU %d (%.04x:%.04x): ", total_cpus, lsapic->eid, lsapic->id);

	if ((lsapic->flags & LSAPIC_ENABLED) == 0) {
		printk("Disabled.\n");
		add = 0;
	}

#ifdef CONFIG_SMP
	smp_boot_data.cpu_phys_id[total_cpus] = -1;
#endif
	if (add) {
		printk("Available.\n");
		available_cpus++;
#ifdef CONFIG_SMP
		smp_boot_data.cpu_phys_id[total_cpus] = (lsapic->id << 8) | lsapic->eid;
#endif /* CONFIG_SMP */
	}
	total_cpus++;
}

/*
 * Info on platform interrupt sources: NMI. PMI, INIT, etc.
 */
static void __init
acpi20_platform (char *p)
{
	acpi20_entry_platform_src_t *plat = (acpi20_entry_platform_src_t *) p;

	printk("PLATFORM: IOSAPIC %x -> Vector %x on CPU %.04u:%.04u\n",
	       plat->iosapic_vector, plat->global_vector, plat->eid, plat->id);
}

/*
 * Override the physical address of the local APIC in the MADT stable header.
 */
static void __init
acpi20_lapic_addr_override (char *p)
{
	acpi20_entry_lapic_addr_override_t * lapic = (acpi20_entry_lapic_addr_override_t *) p;

	if (lapic->lapic_address) {
		iounmap((void *)ipi_base_addr);
		ipi_base_addr = (unsigned long) ioremap(lapic->lapic_address, 0);

		printk("LOCAL ACPI override to 0x%lx(p=0x%lx)\n",
		       ipi_base_addr, lapic->lapic_address);
	}
}

/*
 * Parse the ACPI Multiple APIC Description Table
 */
static void __init
acpi20_parse_madt (acpi_madt_t *madt)
{
	acpi_entry_iosapic_t *iosapic;
	char *p, *end;

	/* Base address of IPI Message Block */
	if (madt->lapic_address) {
		ipi_base_addr = (unsigned long) ioremap(madt->lapic_address, 0);
		printk("Lapic address set to 0x%lx\n", ipi_base_addr);
	} else
		printk("Lapic address set to default 0x%lx\n", ipi_base_addr);

	p = (char *) (madt + 1);
	end = p + (madt->header.length - sizeof(acpi_madt_t));

	/*
	 * Splitted entry parsing to ensure ordering.
	 */

	while (p < end) {
		switch (*p) {
		case ACPI20_ENTRY_LOCAL_APIC_ADDR_OVERRIDE:
			printk("ACPI 2.0 MADT: LOCAL APIC Override\n");
			acpi20_lapic_addr_override(p);
			break;

		case ACPI20_ENTRY_LOCAL_SAPIC:
			printk("ACPI 2.0 MADT: LOCAL SAPIC\n");
			acpi20_lsapic(p);
			break;
	
		case ACPI20_ENTRY_IO_SAPIC:
			iosapic = (acpi_entry_iosapic_t *) p;
			if (iosapic_init)
				iosapic_init(iosapic->address, iosapic->irq_base);
			break;

		case ACPI20_ENTRY_PLATFORM_INT_SOURCE:
			printk("ACPI 2.0 MADT: PLATFORM INT SOUCE\n");
			acpi20_platform(p);
			break;

		case ACPI20_ENTRY_LOCAL_APIC:
			printk("ACPI 2.0 MADT: LOCAL APIC entry\n"); break;
		case ACPI20_ENTRY_IO_APIC:
			printk("ACPI 2.0 MADT: IO APIC entry\n"); break;
		case ACPI20_ENTRY_NMI_SOURCE:
			printk("ACPI 2.0 MADT: NMI SOURCE entry\n"); break;
		case ACPI20_ENTRY_LOCAL_APIC_NMI:
			printk("ACPI 2.0 MADT: LOCAL APIC NMI entry\n"); break;
		case ACPI20_ENTRY_INT_SRC_OVERRIDE:
			break;
		default:
			printk("ACPI 2.0 MADT: unknown entry skip\n"); break;
			break;
		}

		p += p[1];
	}

	p = (char *) (madt + 1);
	end = p + (madt->header.length - sizeof(acpi_madt_t));

	while (p < end) {
		
		switch (*p) {
		case ACPI20_ENTRY_INT_SRC_OVERRIDE:
			printk("ACPI 2.0 MADT: INT SOURCE Override\n");
			acpi_legacy_irq(p);
			break;
		default:
			break;
		}

		p += p[1];
	}

	/* Make bootup pretty */
	printk("      %d CPUs available, %d CPUs total\n",
		available_cpus, total_cpus);
}

int __init 
acpi20_parse (acpi20_rsdp_t *rsdp20)
{
	acpi_xsdt_t *xsdt;
	acpi_desc_table_hdr_t *hdrp;
	int tables, i;

	if (strncmp(rsdp20->signature, ACPI_RSDP_SIG, ACPI_RSDP_SIG_LEN)) {
		printk("ACPI 2.0 RSDP signature incorrect!\n");
		return 0;
	} else {
		printk("ACPI 2.0 Root System Description Ptr at 0x%lx\n",
			(unsigned long)rsdp20);
	}

	xsdt = __va(rsdp20->xsdt);
	hdrp = &xsdt->header;
	if (strncmp(hdrp->signature,
		ACPI_XSDT_SIG, ACPI_XSDT_SIG_LEN)) {
		printk("ACPI 2.0 XSDT signature incorrect. Trying RSDT\n");
		/* RSDT parsing here */
		return 0;
	} else {
		printk("ACPI 2.0 XSDT at 0x%lx (p=0x%lx)\n",
		(unsigned long)xsdt, (unsigned long)rsdp20->xsdt);
	}

	printk("ACPI 2.0: %.6s %.8s %d.%d\n",
		hdrp->oem_id,
		hdrp->oem_table_id,
		hdrp->oem_revision >> 16,
		hdrp->oem_revision & 0xffff);

#ifdef CONFIG_ACPI_KERNEL_CONFIG
	acpi_cf_init((void *)rsdp20);
#endif

	tables =(hdrp->length -sizeof(acpi_desc_table_hdr_t))>>3;

	for (i = 0; i < tables; i++) {
		hdrp = (acpi_desc_table_hdr_t *) __va(readl_unaligned(&xsdt->entry_ptrs[i]));
		printk("        :table %4.4s found\n", hdrp->signature);

		/* Only interested int the MADT table for now ... */
		if (strncmp(hdrp->signature,
			ACPI_MADT_SIG, ACPI_MADT_SIG_LEN) != 0)
			continue;

		acpi20_parse_madt((acpi_madt_t *) hdrp);
	}

#ifdef CONFIG_ACPI_KERNEL_CONFIG
	acpi_cf_terminate();
#endif

#ifdef CONFIG_SMP
	if (available_cpus == 0) {
		printk("ACPI: Found 0 CPUS; assuming 1\n");
		available_cpus = 1; /* We've got at least one of these, no? */
	}
	smp_boot_data.cpu_count = available_cpus;
#endif
	return 1;
}
/*
 * ACPI 1.0b with 0.71 IA64 extensions functions; should be removed once all 
 * platforms start supporting ACPI 2.0
 */

/*
 * Identify usable CPU's and remember them for SMP bringup later.
 */
static void __init
acpi_lsapic (char *p) 
{
	int add = 1;

	acpi_entry_lsapic_t *lsapic = (acpi_entry_lsapic_t *) p;

	if ((lsapic->flags & LSAPIC_PRESENT) == 0) 
		return;

	printk("      CPU %d (%.04x:%.04x): ", total_cpus, lsapic->eid, lsapic->id);

	if ((lsapic->flags & LSAPIC_ENABLED) == 0) {
		printk("Disabled.\n");
		add = 0;
	} else if (lsapic->flags & LSAPIC_PERFORMANCE_RESTRICTED) {
		printk("Performance Restricted; ignoring.\n");
		add = 0;
	}

#ifdef CONFIG_SMP
	smp_boot_data.cpu_phys_id[total_cpus] = -1;
#endif
	if (add) {
		printk("Available.\n");
		available_cpus++;
#ifdef CONFIG_SMP
		smp_boot_data.cpu_phys_id[total_cpus] = (lsapic->id << 8) | lsapic->eid;
#endif /* CONFIG_SMP */
	}
	total_cpus++;
}

/*
 * Info on platform interrupt sources: NMI. PMI, INIT, etc.
 */
static void __init
acpi_platform (char *p)
{
	acpi_entry_platform_src_t *plat = (acpi_entry_platform_src_t *) p;

	printk("PLATFORM: IOSAPIC %x -> Vector %x on CPU %.04u:%.04u\n",
	       plat->iosapic_vector, plat->global_vector, plat->eid, plat->id);
}

/*
 * Parse the ACPI Multiple SAPIC Table
 */
static void __init
acpi_parse_msapic (acpi_sapic_t *msapic)
{
	acpi_entry_iosapic_t *iosapic;
	char *p, *end;

	/* Base address of IPI Message Block */
	ipi_base_addr = (unsigned long) ioremap(msapic->interrupt_block, 0);

	p = (char *) (msapic + 1);
	end = p + (msapic->header.length - sizeof(acpi_sapic_t));

	while (p < end) {
		switch (*p) {
		      case ACPI_ENTRY_LOCAL_SAPIC:
			acpi_lsapic(p);
			break;
	
		      case ACPI_ENTRY_IO_SAPIC:
			iosapic = (acpi_entry_iosapic_t *) p;
			if (iosapic_init)
				iosapic_init(iosapic->address, iosapic->irq_base);
			break;

		      case ACPI_ENTRY_INT_SRC_OVERRIDE:
			acpi_legacy_irq(p);
			break;

		      case ACPI_ENTRY_PLATFORM_INT_SOURCE:
			acpi_platform(p);
			break;

		      default:
			break;
		}

		/* Move to next table entry. */
		p += p[1];
	}

	/* Make bootup pretty */
	printk("      %d CPUs available, %d CPUs total\n", available_cpus, total_cpus);
}

int __init 
acpi_parse (acpi_rsdp_t *rsdp)
{
	acpi_rsdt_t *rsdt;
	acpi_desc_table_hdr_t *hdrp;
	long tables, i;

	if (strncmp(rsdp->signature, ACPI_RSDP_SIG, ACPI_RSDP_SIG_LEN)) {
		printk("Uh-oh, ACPI RSDP signature incorrect!\n");
		return 0;
	}

	rsdt = __va(rsdp->rsdt);
	if (strncmp(rsdt->header.signature, ACPI_RSDT_SIG, ACPI_RSDT_SIG_LEN)) {
		printk("Uh-oh, ACPI RDST signature incorrect!\n");
		return 0;
	}

	printk("ACPI: %.6s %.8s %d.%d\n", rsdt->header.oem_id, rsdt->header.oem_table_id, 
	       rsdt->header.oem_revision >> 16, rsdt->header.oem_revision & 0xffff);
	
#ifdef CONFIG_ACPI_KERNEL_CONFIG
	acpi_cf_init(rsdp);
#endif

	tables = (rsdt->header.length - sizeof(acpi_desc_table_hdr_t)) / 8;
	for (i = 0; i < tables; i++) {
		hdrp = (acpi_desc_table_hdr_t *) __va(rsdt->entry_ptrs[i]);

		/* Only interested int the MSAPIC table for now ... */
		if (strncmp(hdrp->signature, ACPI_SAPIC_SIG, ACPI_SAPIC_SIG_LEN) != 0)
			continue;

		acpi_parse_msapic((acpi_sapic_t *) hdrp);
	}

#ifdef CONFIG_ACPI_KERNEL_CONFIG
	acpi_cf_terminate();
#endif

#ifdef CONFIG_SMP
	if (available_cpus == 0) {
		printk("ACPI: Found 0 CPUS; assuming 1\n");
		available_cpus = 1; /* We've got at least one of these, no? */
	}
	smp_boot_data.cpu_count = available_cpus;
#endif
	return 1;
}
