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
#include <asm/bootx.h>
#include <asm/btext.h>
#include <asm/cputable.h>
#include <asm/pmac_feature.h>
#include <asm/time.h>
#include <asm/of_device.h>
#include <asm/lmb.h>

#include "pmac.h"

static int current_root_goodness = -1;
#define DEFAULT_ROOT_DEVICE Root_SDA1	/* sda1 - slightly silly choice */

extern  int powersave_nap;
int sccdbg;

extern void udbg_init_scc(struct device_node *np);

#ifdef CONFIG_BOOTX_TEXT
void pmac_progress(char *s, unsigned short hex);
#endif

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
	struct device_node *cpu;
	int *fp;
	unsigned long pvr;

	pvr = PVR_VER(mfspr(PVR));

	/* Set loops_per_jiffy to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	cpu = find_type_devices("cpu");
	if (cpu != 0) {
		fp = (int *) get_property(cpu, "clock-frequency", NULL);
		if (fp != 0) {
			if (pvr == 4 || pvr >= 8)
				/* 604, G3, G4 etc. */
				loops_per_jiffy = *fp / HZ;
			else
				/* 601, 603, etc. */
				loops_per_jiffy = *fp / (2*HZ);
		} else
			loops_per_jiffy = 50000000 / HZ;
	}
	
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

extern char *bootpath;
extern char *bootdevice;
void *boot_host;
int boot_target;
int boot_part;
extern dev_t boot_dev;

#ifdef CONFIG_SCSI
void __init note_scsi_host(struct device_node *node, void *host)
{
	int l;
	char *p;

	l = strlen(node->full_name);
	if (bootpath != NULL && bootdevice != NULL
	    && strncmp(node->full_name, bootdevice, l) == 0
	    && (bootdevice[l] == '/' || bootdevice[l] == 0)) {
		boot_host = host;
		/*
		 * There's a bug in OF 1.0.5.  (Why am I not surprised.)
		 * If you pass a path like scsi/sd@1:0 to canon, it returns
		 * something like /bandit@F2000000/gc@10/53c94@10000/sd@0,0
		 * That is, the scsi target number doesn't get preserved.
		 * So we pick the target number out of bootpath and use that.
		 */
		p = strstr(bootpath, "/sd@");
		if (p != NULL) {
			p += 4;
			boot_target = simple_strtoul(p, NULL, 10);
			p = strchr(p, ':');
			if (p != NULL)
				boot_part = simple_strtoul(p + 1, NULL, 10);
		}
	}
}
#endif

#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
static dev_t __init find_ide_boot(void)
{
	char *p;
	int n;
	dev_t __init pmac_find_ide_boot(char *bootdevice, int n);

	if (bootdevice == NULL)
		return 0;
	p = strrchr(bootdevice, '/');
	if (p == NULL)
		return 0;
	n = p - bootdevice;

	return pmac_find_ide_boot(bootdevice, n);
}
#endif /* CONFIG_BLK_DEV_IDE && CONFIG_BLK_DEV_IDE_PMAC */

void __init find_boot_device(void)
{
#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
	boot_dev = find_ide_boot();
#endif
}

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
	static int found_boot = 0;
	char *p;

	if (!initializing)
		return;
	if ((goodness <= current_root_goodness) &&
	    ROOT_DEV != DEFAULT_ROOT_DEVICE)
		return;
	p = strstr(saved_command_line, "root=");
	if (p != NULL && (p == saved_command_line || p[-1] == ' '))
		return;

	if (!found_boot) {
		find_boot_device();
		found_boot = 1;
	}
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

/* 
 * Early initialization.
 * Relocation is on but do not reference unbolted pages
 * Also, device-tree hasn't been "finished", so don't muck with
 * it too much
 */
void __init pmac_init_early(void)
{
	hpte_init_pSeries();

#ifdef CONFIG_BOOTX_TEXT
	ppc_md.udbg_putc = btext_putc;
	ppc_md.udbg_getc = dummy_getc;
	ppc_md.udbg_getc_poll = dummy_getc_poll;
#endif /* CONFIG_BOOTX_TEXT */
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

void __init pmac_init(unsigned long r3, unsigned long r4, unsigned long r5,
		      unsigned long r6, unsigned long r7)
{
	/* Probe motherboard chipset */
	pmac_feature_init();

	/* Init SCC */
	if (strstr(cmd_line, "sccdbg")) {
		sccdbg = 1;
		udbg_init_scc(NULL);
	}

	/* Fill up the machine description */
	ppc_md.setup_arch     = pmac_setup_arch;
       	ppc_md.get_cpuinfo    = pmac_show_cpuinfo;

	ppc_md.init_IRQ       = pmac_init_IRQ;
	ppc_md.get_irq        = openpic_get_irq;

	ppc_md.pcibios_fixup  = pmac_pcibios_fixup;

	ppc_md.restart        = pmac_restart;
	ppc_md.power_off      = pmac_power_off;
	ppc_md.halt           = pmac_halt;

       	ppc_md.get_boot_time  = pmac_get_boot_time;
       	ppc_md.set_rtc_time   = pmac_set_rtc_time;
       	ppc_md.get_rtc_time   = pmac_get_rtc_time;
      	ppc_md.calibrate_decr = pmac_calibrate_decr;

	ppc_md.feature_call   = pmac_do_feature_call;


#ifdef CONFIG_BOOTX_TEXT
	ppc_md.progress       = pmac_progress;
#endif /* CONFIG_BOOTX_TEXT */

	if (ppc_md.progress) ppc_md.progress("pmac_init(): exit", 0);

}

#ifdef CONFIG_BOOTX_TEXT
void __init pmac_progress(char *s, unsigned short hex)
{
	if (sccdbg) {
		udbg_puts(s);
		udbg_putc('\n');
	}
	else if (boot_text_mapped) {
		btext_drawstring(s);
		btext_drawchar('\n');
	}
}
#endif /* CONFIG_BOOTX_TEXT */

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
