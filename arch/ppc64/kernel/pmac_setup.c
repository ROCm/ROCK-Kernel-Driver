/*
 *  arch/ppc/platforms/setup.c
 *
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  Derived from "arch/alpha/kernel/setup.c"
 *    Copyright (C) 1995 Linus Torvalds
 *
 *  Maintained by Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

/*
 * bootup setup stuff..
 */

#undef DEBUG

#include <linux/config.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/vt_kern.h>
#include <linux/console.h>
#include <linux/ide.h>
#include <linux/pci.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/pmu.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/machdep.h>
#include <asm/dma.h>
#include <asm/btext.h>
#include <asm/cputable.h>
#include <asm/pmac_feature.h>
#include <asm/time.h>
#include <asm/of_device.h>
#include <asm/lmb.h>

#include "pmac.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif


static int current_root_goodness = -1;
#define DEFAULT_ROOT_DEVICE Root_SDA1	/* sda1 - slightly silly choice */

extern  int powersave_nap;
int sccdbg;

extern void udbg_init_scc(struct device_node *np);

void __pmac pmac_show_cpuinfo(struct seq_file *m)
{
	struct device_node *np;
	char *pp;
	int plen;
	char* mbname;
	int mbmodel = pmac_call_feature(PMAC_FTR_GET_MB_INFO, NULL,
					PMAC_MB_INFO_MODEL, 0);
	unsigned int mbflags = pmac_call_feature(PMAC_FTR_GET_MB_INFO, NULL,
						 PMAC_MB_INFO_FLAGS, 0);

	if (pmac_call_feature(PMAC_FTR_GET_MB_INFO, NULL, PMAC_MB_INFO_NAME,
			      (long)&mbname) != 0)
		mbname = "Unknown";
	
	/* find motherboard type */
	seq_printf(m, "machine\t\t: ");
	np = find_devices("device-tree");
	if (np != NULL) {
		pp = (char *) get_property(np, "model", NULL);
		if (pp != NULL)
			seq_printf(m, "%s\n", pp);
		else
			seq_printf(m, "PowerMac\n");
		pp = (char *) get_property(np, "compatible", &plen);
		if (pp != NULL) {
			seq_printf(m, "motherboard\t:");
			while (plen > 0) {
				int l = strlen(pp) + 1;
				seq_printf(m, " %s", pp);
				plen -= l;
				pp += l;
			}
			seq_printf(m, "\n");
		}
	} else
		seq_printf(m, "PowerMac\n");

	/* print parsed model */
	seq_printf(m, "detected as\t: %d (%s)\n", mbmodel, mbname);
	seq_printf(m, "pmac flags\t: %08x\n", mbflags);

	/* Indicate newworld */
	seq_printf(m, "pmac-generation\t: NewWorld\n");
}


void __init pmac_setup_arch(void)
{
	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	/* Probe motherboard chipset */
	pmac_feature_init();
#if 0
	/* Lock-enable the SCC channel used for debug */
	if (sccdbg) {
		np = of_find_node_by_name(NULL, "escc");
		if (np)
			pmac_call_feature(PMAC_FTR_SCC_ENABLE, np,
					  PMAC_SCC_ASYNC | PMAC_SCC_FLAG_XMON, 1);
	}
#endif
	/* We can NAP */
	powersave_nap = 1;

	/* Initialize the PMU */
	find_via_pmu();

	/* Init NVRAM access */
	pmac_nvram_init();

	/* Setup SMP callback */
#ifdef CONFIG_SMP
	pmac_setup_smp();
#endif

	/* Setup the PCI DMA to "direct" by default. May be overriden
	 * by iommu later on
	 */
	pci_dma_init_direct();

	/* Lookup PCI hosts */
       	pmac_pci_init();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif
}

#ifdef CONFIG_SCSI
void note_scsi_host(struct device_node *node, void *host)
{
	/* Obsolete */
}
#endif


static int initializing = 1;

static int pmac_late_init(void)
{
	initializing = 0;
	return 0;
}

late_initcall(pmac_late_init);

/* can't be __init - can be called whenever a disk is first accessed */
void __pmac note_bootable_part(dev_t dev, int part, int goodness)
{
	extern dev_t boot_dev;
	char *p;

	if (!initializing)
		return;
	if ((goodness <= current_root_goodness) &&
	    ROOT_DEV != DEFAULT_ROOT_DEVICE)
		return;
	p = strstr(saved_command_line, "root=");
	if (p != NULL && (p == saved_command_line || p[-1] == ' '))
		return;

	if (!boot_dev || dev == boot_dev) {
		ROOT_DEV = dev + part;
		boot_dev = 0;
		current_root_goodness = goodness;
	}
}

void __pmac pmac_restart(char *cmd)
{
	pmu_restart();
}

void __pmac pmac_power_off(void)
{
	pmu_shutdown();
}

void __pmac pmac_halt(void)
{
	pmac_power_off();
}

#ifdef CONFIG_BOOTX_TEXT
static int dummy_getc_poll(void)
{
	return -1;
}

static unsigned char dummy_getc(void)
{
	return 0;
}

static void btext_putc(unsigned char c)
{
	btext_drawchar(c);
}
#endif /* CONFIG_BOOTX_TEXT */

static void __init init_boot_display(void)
{
	char *name;
	struct device_node *np = NULL; 
	int rc = -ENODEV;

	printk("trying to initialize btext ...\n");

	name = (char *)get_property(of_chosen, "linux,stdout-path", NULL);
	if (name != NULL) {
		np = of_find_node_by_path(name);
		if (np != NULL) {
			if (strcmp(np->type, "display") != 0) {
				printk("boot stdout isn't a display !\n");
				of_node_put(np);
				np = NULL;
			}
		}
	}
	if (np)
		rc = btext_initialize(np);
	if (rc == 0)
		return;

	for (np = NULL; (np = of_find_node_by_type(np, "display"));) {
		if (get_property(np, "linux,opened", NULL)) {
			printk("trying %s ...\n", np->full_name);
			rc = btext_initialize(np);
			printk("result: %d\n", rc);
		}
		if (rc == 0)
			return;
	}
}

/* 
 * Early initialization.
 */
void __init pmac_init_early(void)
{
	DBG(" -> pmac_init_early\n");

	/* Initialize hash table, from now on, we can take hash faults
	 * and call ioremap
	 */
	hpte_init_native();

	/* Init SCC */
       	if (strstr(cmd_line, "sccdbg")) {
		sccdbg = 1;
       		udbg_init_scc(NULL);
       	}

	else {
#ifdef CONFIG_BOOTX_TEXT
		init_boot_display();

		ppc_md.udbg_putc = btext_putc;
		ppc_md.udbg_getc = dummy_getc;
		ppc_md.udbg_getc_poll = dummy_getc_poll;
#endif /* CONFIG_BOOTX_TEXT */
	}

	/* Setup interrupt mapping options */
	naca->interrupt_controller = IC_OPEN_PIC;

	DBG(" <- pmac_init_early\n");
}

extern void* OpenPIC_Addr;
extern void* OpenPIC2_Addr;
extern u_int OpenPIC_NumInitSenses;
extern u_char *OpenPIC_InitSenses;
extern void openpic_init(int main_pic, int offset, unsigned char* chrp_ack,
			 int programmer_switch_irq);
extern void openpic2_init(int offset);
extern int openpic_get_irq(struct pt_regs *regs);
extern int openpic2_get_irq(struct pt_regs *regs);

static int pmac_cascade_irq = -1;

static irqreturn_t pmac_u3_do_cascade(int cpl, void *dev_id, struct pt_regs *regs)
{
	int irq;

	for (;;) {
		irq = openpic2_get_irq(regs);
		if (irq == -1)
			break;
		ppc_irq_dispatch_handler(regs, irq);
	}
	return IRQ_HANDLED;
}

static __init void pmac_init_IRQ(void)
{
        struct device_node *irqctrler  = NULL;
        struct device_node *irqctrler2 = NULL;
	struct device_node *np = NULL;

	/* We first try to detect Apple's new Core99 chipset, since mac-io
	 * is quite different on those machines and contains an IBM MPIC2.
	 */
	while ((np = of_find_node_by_type(np, "open-pic")) != NULL) {
		struct device_node *parent = of_get_parent(np);
		if (parent && !strcmp(parent->name, "u3"))
			irqctrler2 = of_node_get(np);
		else
			irqctrler = of_node_get(np);
		of_node_put(parent);
	}
	if (irqctrler != NULL && irqctrler->n_addrs > 0) {
		unsigned char senses[128];

		printk(KERN_INFO "PowerMac using OpenPIC irq controller at 0x%08x\n",
		       (unsigned int)irqctrler->addrs[0].address);

		prom_get_irq_senses(senses, 0, 128);
		OpenPIC_InitSenses = senses;
		OpenPIC_NumInitSenses = 128;
		OpenPIC_Addr = ioremap(irqctrler->addrs[0].address,
				       irqctrler->addrs[0].size);
		openpic_init(1, 0, NULL, -1);

		if (irqctrler2 != NULL && irqctrler2->n_intrs > 0 &&
		    irqctrler2->n_addrs > 0) {
			printk(KERN_INFO "Slave OpenPIC at 0x%08x hooked on IRQ %d\n",
			       (u32)irqctrler2->addrs[0].address,
			       irqctrler2->intrs[0].line);
			pmac_call_feature(PMAC_FTR_ENABLE_MPIC, irqctrler2, 0, 0);
			OpenPIC2_Addr = ioremap(irqctrler2->addrs[0].address,
						irqctrler2->addrs[0].size);
			prom_get_irq_senses(senses, 128, 128 + 128);
			OpenPIC_InitSenses = senses;
			OpenPIC_NumInitSenses = 128;
			openpic2_init(128);
			pmac_cascade_irq = irqctrler2->intrs[0].line;
		}
	}
	of_node_put(irqctrler);
	of_node_put(irqctrler2);
}

/* We cannot do request_irq too early ... Right now, we get the
 * cascade as a core_initcall, which should be fine for our needs
 */
static int __init pmac_irq_cascade_init(void)
{
	if (request_irq(pmac_cascade_irq, pmac_u3_do_cascade, 0,
			"U3->K2 Cascade", NULL))
		printk(KERN_ERR "Unable to get OpenPIC IRQ for cascade\n");
	return 0;
}

core_initcall(pmac_irq_cascade_init);

static void __init pmac_progress(char *s, unsigned short hex)
{
	if (sccdbg) {
		udbg_puts(s);
		udbg_puts("\n");
	}
#ifdef CONFIG_BOOTX_TEXT
	else if (boot_text_mapped) {
		btext_drawstring(s);
		btext_drawstring("\n");
	}
#endif /* CONFIG_BOOTX_TEXT */
}

static int __init pmac_declare_of_platform_devices(void)
{
	struct device_node *np;

	np = find_devices("u3");
	if (np) {
		for (np = np->child; np != NULL; np = np->sibling)
			if (strncmp(np->name, "i2c", 3) == 0) {
				of_platform_device_create(np, "u3-i2c");
				break;
			}
	}

	return 0;
}

device_initcall(pmac_declare_of_platform_devices);

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init pmac_probe(int platform)
{
	if (platform != PLATFORM_POWERMAC)
		return 0;

	/*
	 * On U3, the DART (iommu) must be allocated now since it
	 * has an impact on htab_initialize (due to the large page it
	 * occupies having to be broken up so the DART itself is not
	 * part of the cacheable linar mapping
	 */
	alloc_u3_dart_table();

	return 1;
}

struct machdep_calls __initdata pmac_md = {
	.probe			= pmac_probe,
	.setup_arch		= pmac_setup_arch,
	.init_early		= pmac_init_early,
       	.get_cpuinfo		= pmac_show_cpuinfo,
	.init_IRQ		= pmac_init_IRQ,
	.get_irq		= openpic_get_irq,
	.pcibios_fixup		= pmac_pcibios_fixup,
	.restart		= pmac_restart,
	.power_off		= pmac_power_off,
	.halt			= pmac_halt,
       	.get_boot_time		= pmac_get_boot_time,
       	.set_rtc_time		= pmac_set_rtc_time,
       	.get_rtc_time		= pmac_get_rtc_time,
      	.calibrate_decr		= pmac_calibrate_decr,
	.feature_call		= pmac_do_feature_call,
	.progress		= pmac_progress,
};
