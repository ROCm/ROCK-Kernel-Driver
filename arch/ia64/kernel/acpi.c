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
 * Copyright (C) 2000,2001 J.I. Lee <jung-ik.lee@intel.com>
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
#ifdef CONFIG_SERIAL_ACPI
#include <linux/acpi_serial.h>
#endif

#include <asm/acpi-ext.h>
#include <asm/acpikcfg.h>
#include <asm/efi.h>
#include <asm/io.h>
#include <asm/iosapic.h>
#include <asm/machvec.h>
#include <asm/page.h>

#undef ACPI_DEBUG		/* Guess what this does? */

/* global array to record platform interrupt vectors for generic int routing */
int platform_irq_list[ACPI_MAX_PLATFORM_IRQS];

/* These are ugly but will be reclaimed by the kernel */
int __initdata available_cpus;
int __initdata total_cpus;

void (*pm_idle) (void);
void (*pm_power_off) (void);

asm (".weak iosapic_register_irq");
asm (".weak iosapic_register_legacy_irq");
asm (".weak iosapic_register_platform_irq");
asm (".weak iosapic_init");
asm (".weak iosapic_version");

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
# elif defined (CONFIG_IA64_SGI_SN2)
	return "sn2";
# elif defined (CONFIG_IA64_DIG)
	return "dig";
# else
#	error Unknown platform.  Fix acpi.c.
# endif
#endif

}

/*
 * Interrupt routing API for device drivers.
 * Provides the interrupt vector for a generic platform event
 * (currently only CPEI implemented)
 */
int
acpi_request_vector(u32 int_type)
{
	int vector = -1;

	if (int_type < ACPI_MAX_PLATFORM_IRQS) {
		/* correctable platform error interrupt */
		vector = platform_irq_list[int_type];
	} else
		printk("acpi_request_vector(): invalid interrupt type\n");

	return vector;
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
	printk("      CPU %.04x:%.04x: ", lsapic->eid, lsapic->id);

	if ((lsapic->flags & LSAPIC_ENABLED) == 0) {
		printk("disabled.\n");
		add = 0;
	}

#ifdef CONFIG_SMP
	smp_boot_data.cpu_phys_id[total_cpus] = -1;
#endif
	if (add) {
		available_cpus++;
		printk("available");
#ifdef CONFIG_SMP
		smp_boot_data.cpu_phys_id[total_cpus] = (lsapic->id << 8) | lsapic->eid;
		if (hard_smp_processor_id() == smp_boot_data.cpu_phys_id[total_cpus])
			printk(" (BSP)");
#endif
		printk(".\n");
	}
	total_cpus++;
}

/*
 * Extract iosapic info from madt (again) to determine which iosapic
 * this platform interrupt resides in
 */
static int __init
acpi20_which_iosapic (int global_vector, acpi_madt_t *madt, u32 *irq_base, char **iosapic_address)
{
	acpi_entry_iosapic_t *iosapic;
	char *p, *end;
	int ver, max_pin;

	p = (char *) (madt + 1);
	end = p + (madt->header.length - sizeof(acpi_madt_t));

	while (p < end) {
		switch (*p) {
		      case ACPI20_ENTRY_IO_SAPIC:
			/* collect IOSAPIC info for platform int use later */
			iosapic = (acpi_entry_iosapic_t *)p;
			*irq_base = iosapic->irq_base;
			*iosapic_address = ioremap(iosapic->address, 0);
			/* is this the iosapic we're looking for? */
			ver = iosapic_version(*iosapic_address);
			max_pin = (ver >> 16) & 0xff;
			if ((global_vector - *irq_base) <= max_pin)
				return 0;		/* found it! */
			break;
		      default:
			break;
		}
		p += p[1];
	}
	return 1;
}

/*
 * Info on platform interrupt sources: NMI, PMI, INIT, etc.
 */
static void __init
acpi20_platform (char *p, acpi_madt_t *madt)
{
	int vector;
	u32 irq_base;
	char *iosapic_address;
	unsigned long polarity = 0, trigger = 0;
	acpi20_entry_platform_src_t *plat = (acpi20_entry_platform_src_t *) p;

	printk("PLATFORM: IOSAPIC %x -> Vector %x on CPU %.04u:%.04u\n",
	       plat->iosapic_vector, plat->global_vector, plat->eid, plat->id);

	/* record platform interrupt vectors for generic int routing code */

	if (!iosapic_register_platform_irq) {
		printk("acpi20_platform(): no ACPI platform IRQ support\n");
		return;
	}

	/* extract polarity and trigger info from flags */
	switch (plat->flags) {
	      case 0x5: polarity = 1; trigger = 1; break;
	      case 0x7: polarity = 0; trigger = 1; break;
	      case 0xd: polarity = 1; trigger = 0; break;
	      case 0xf: polarity = 0; trigger = 0; break;
	      default:
		printk("acpi20_platform(): unknown flags 0x%x\n", plat->flags);
		break;
	}

	/* which iosapic does this IRQ belong to? */
	if (acpi20_which_iosapic(plat->global_vector, madt, &irq_base, &iosapic_address)) {
		printk("acpi20_platform(): I/O SAPIC not found!\n");
		return;
	}

	/*
	 * get vector assignment for this IRQ, set attributes, and program the IOSAPIC
	 * routing table
	 */
	vector = iosapic_register_platform_irq(plat->int_type,
					       plat->global_vector,
					       plat->iosapic_vector,
					       plat->eid,
					       plat->id,
					       polarity,
					       trigger,
					       irq_base,
					       iosapic_address);
	platform_irq_list[plat->int_type] = vector;
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
	acpi_entry_iosapic_t *iosapic = NULL;
	acpi20_entry_lsapic_t *lsapic = NULL;
	char *p, *end;
	int i;

	/* Base address of IPI Message Block */
	if (madt->lapic_address) {
		ipi_base_addr = (unsigned long) ioremap(madt->lapic_address, 0);
		printk("Lapic address set to 0x%lx\n", ipi_base_addr);
	} else
		printk("Lapic address set to default 0x%lx\n", ipi_base_addr);

	p = (char *) (madt + 1);
	end = p + (madt->header.length - sizeof(acpi_madt_t));

	/* Initialize platform interrupt vector array */
	for (i = 0; i < ACPI_MAX_PLATFORM_IRQS; i++)
		platform_irq_list[i] = -1;

	/*
	 * Split-up entry parsing to ensure ordering.
	 */
	while (p < end) {
		switch (*p) {
		      case ACPI20_ENTRY_LOCAL_APIC_ADDR_OVERRIDE:
			printk("ACPI 2.0 MADT: LOCAL APIC Override\n");
			acpi20_lapic_addr_override(p);
			break;

		      case ACPI20_ENTRY_LOCAL_SAPIC:
			printk("ACPI 2.0 MADT: LOCAL SAPIC\n");
			lsapic = (acpi20_entry_lsapic_t *) p;
			acpi20_lsapic(p);
			break;

		      case ACPI20_ENTRY_IO_SAPIC:
			iosapic = (acpi_entry_iosapic_t *) p;
			if (iosapic_init)
				/*
				 * The PCAT_COMPAT flag indicates that the system has a
				 * dual-8259 compatible setup.
				 */
				iosapic_init(iosapic->address, iosapic->irq_base,
#ifdef CONFIG_ITANIUM
					     1 /* fw on some Itanium systems is broken... */
#else
					     (madt->flags & MADT_PCAT_COMPAT)
#endif
					);
			break;

		      case ACPI20_ENTRY_PLATFORM_INT_SOURCE:
			printk("ACPI 2.0 MADT: PLATFORM INT SOURCE\n");
			acpi20_platform(p, madt);
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
		      case ACPI20_ENTRY_LOCAL_APIC:
			if (lsapic) break;
			printk("ACPI 2.0 MADT: LOCAL APIC entry\n");
			/* parse local apic if there's no local Sapic */
			break;
		      case ACPI20_ENTRY_IO_APIC:
			if (iosapic) break;
			printk("ACPI 2.0 MADT: IO APIC entry\n");
			/* parse ioapic if there's no ioSapic */
			break;
		      default:
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
# ifdef CONFIG_ACPI
	acpi_xsdt_t *xsdt;
	acpi_desc_table_hdr_t *hdrp;
	acpi_madt_t *madt;
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

	acpi_cf_init((void *)rsdp20);

	tables =(hdrp->length -sizeof(acpi_desc_table_hdr_t))>>3;

	for (i = 0; i < tables; i++) {
		hdrp = (acpi_desc_table_hdr_t *) __va(readl_unaligned(&xsdt->entry_ptrs[i]));
		printk("        :table %4.4s found\n", hdrp->signature);

		/* Only interested int the MADT table for now ... */
		if (strncmp(hdrp->signature,
			ACPI_MADT_SIG, ACPI_MADT_SIG_LEN) != 0)
			continue;

		/* Save MADT pointer for later */
		madt = (acpi_madt_t *) hdrp;
		acpi20_parse_madt(madt);
	}

#ifdef CONFIG_SERIAL_ACPI
	/*
	 * Now we're interested in other tables.  We want the iosapics already
	 * initialized, so we do it in a separate loop.
	 */
	for (i = 0; i < tables; i++) {
		hdrp = (acpi_desc_table_hdr_t *) __va(readl_unaligned(&xsdt->entry_ptrs[i]));
		/*
		 * search for SPCR and DBGP table entries so we can enable
		 * non-pci interrupts to IO-SAPICs.
		 */
		if (!strncmp(hdrp->signature, ACPI_SPCRT_SIG, ACPI_SPCRT_SIG_LEN) ||
		    !strncmp(hdrp->signature, ACPI_DBGPT_SIG, ACPI_DBGPT_SIG_LEN))
		{
			acpi_ser_t *spcr = (void *)hdrp;
			unsigned long global_int;

			setup_serial_acpi(hdrp);

			/*
			 * ACPI is able to describe serial ports that live at non-standard
			 * memory space addresses and use SAPIC interrupts.  If not also
			 * PCI devices, there would be no interrupt vector information for
			 * them.  This checks for and fixes that situation.
			 */
			if (spcr->length < sizeof(acpi_ser_t))
				/* table is not long enough for full info, thus no int */
				break;

			/*
			 * If the device is not in PCI space, but uses a SAPIC interrupt,
			 * we need to program the SAPIC so that serial can autoprobe for
			 * the IA64 interrupt vector later on.  If the device is in PCI
			 * space, it should already be setup via the PCI vectors
			 */
			if (spcr->base_addr.space_id != ACPI_SERIAL_PCICONF_SPACE &&
			    spcr->int_type == ACPI_SERIAL_INT_SAPIC)
			{
				u32 irq_base;
				char *iosapic_address;
				int vector;

				/* We have a UART in memory space with a SAPIC interrupt */
				global_int = (  (spcr->global_int[3] << 24)
					      | (spcr->global_int[2] << 16)
					      | (spcr->global_int[1] << 8)
					      | spcr->global_int[0]);

				if (!iosapic_register_irq)
					continue;

				/* which iosapic does this IRQ belong to? */
				if (acpi20_which_iosapic(global_int, madt, &irq_base,
							 &iosapic_address) == 0)
				{
					vector = iosapic_register_irq(global_int,
					                              1, /* active high polarity */
					                              1, /* edge triggered */
					                              irq_base,
					                              iosapic_address);
				}
			}
		}
	}
#endif
	acpi_cf_terminate();

#  ifdef CONFIG_SMP
	if (available_cpus == 0) {
		printk("ACPI: Found 0 CPUS; assuming 1\n");
		available_cpus = 1; /* We've got at least one of these, no? */
	}
	smp_boot_data.cpu_count = total_cpus;
#  endif
# endif /* CONFIG_ACPI */
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
				/*
				 * The ACPI I/O SAPIC table doesn't have a PCAT_COMPAT
				 * flag like the MADT table, but we can safely assume that
				 * ACPI 1.0b systems have a dual-8259 setup.
				 */
				iosapic_init(iosapic->address, iosapic->irq_base, 1);
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
# ifdef CONFIG_ACPI
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

	acpi_cf_init(rsdp);

	tables = (rsdt->header.length - sizeof(acpi_desc_table_hdr_t)) / 8;
	for (i = 0; i < tables; i++) {
		hdrp = (acpi_desc_table_hdr_t *) __va(rsdt->entry_ptrs[i]);

		/* Only interested int the MSAPIC table for now ... */
		if (strncmp(hdrp->signature, ACPI_SAPIC_SIG, ACPI_SAPIC_SIG_LEN) != 0)
			continue;

		acpi_parse_msapic((acpi_sapic_t *) hdrp);
	}

	acpi_cf_terminate();

#  ifdef CONFIG_SMP
	if (available_cpus == 0) {
		printk("ACPI: Found 0 CPUS; assuming 1\n");
		available_cpus = 1; /* We've got at least one of these, no? */
	}
	smp_boot_data.cpu_count = total_cpus;
#  endif
# endif /* CONFIG_ACPI */
	return 1;
}
