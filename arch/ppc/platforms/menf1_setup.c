/*
 * arch/ppc/platforms/menf1_setup.c
 *
 * Board setup routines for MEN F1
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/blk.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/i8259.h>
#include <asm/mpc10x.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>

#include "menf1.h"

extern void menf1_find_bridges(void);
extern unsigned long loops_per_jiffy;

/* Dummy variable to satisfy mpc10x_common.o */
void *OpenPIC_Addr;

static int
menf1_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "machine\t\t: MEN F1\n");

	return 0;
}

static void __init
menf1_setup_arch(void)
{
	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000/HZ;

	/* Lookup PCI host bridges */
	menf1_find_bridges();

#ifdef CONFIG_BLK_DEV_INITRD
        if (initrd_start)
                ROOT_DEV = Root_RAM0;
        else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_HDA2;
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	printk("MEN F1 port (C) 2001 MontaVista Software, Inc. (source@mvista.com)\n");
}

static void
menf1_restart(char *cmd)
{

       int     picr1;
       struct pci_dev *pdev;

	local_irq_disable();

	/*
	 * Firmware doesn't like re-entry using Map B (CHRP), so make sure the
	 * PCI bridge is using MAP A (PReP).
	 */

	pdev = pci_find_slot(0, PCI_DEVFN(0,0));

	while(pdev == NULL); /* paranoia */

	pci_read_config_dword(pdev, MPC10X_CFG_PICR1_REG, &picr1);

	picr1 = (picr1 & ~MPC10X_CFG_PICR1_ADDR_MAP_MASK) |
		MPC10X_CFG_PICR1_ADDR_MAP_A;

	pci_write_config_dword(pdev, MPC10X_CFG_PICR1_REG, picr1);

	asm volatile("sync");

	/* SRR0 has system reset vector, SRR1 has default MSR value */
	/* rfi restores MSR from SRR1 and sets the PC to the SRR0 value */
	__asm__ __volatile__
	("\n\
	lis     3,0xfff0
	ori     3,3,0x0100
	mtspr   26,3
	li      3,0
	mtspr   27,3
	rfi
	");
	while(1);
}

static void
menf1_halt(void)
{
	local_irq_disable();
	while (1);
}

static void
menf1_power_off(void)
{
	menf1_halt();
}

static void __init
menf1_init_IRQ(void)
{
	int i;

	for ( i = 0 ; i < NUM_8259_INTERRUPTS  ; i++ )
		irq_desc[i].handler = &i8259_pic;
	i8259_init(NULL);
}

/*
 * Set BAT 3 to map 0xF0000000.
 */
static __inline__ void
menf1_set_bat(void)
{
	static int	mapping_set = 0;

	if (!mapping_set)
	{

		/* wait for all outstanding memory accesses to complete */
		mb();

		/* setup DBATs */
		mtspr(DBAT3U, 0xf0001ffe);
		mtspr(DBAT3L, 0xf000002a);

		/* wait for updates */
		mb();

		mapping_set = 1;
	}
	return;
}

static unsigned long __init
menf1_find_end_of_memory(void)
{
	/* Cover the I/O with a BAT */
	menf1_set_bat();

	/* Read the memory size from the MPC107 SMC */
	return mpc10x_get_mem_size(MPC10X_MEM_MAP_B);
}

static void __init
menf1_map_io(void)
{
        io_block_mapping(0xfe000000, 0xfe000000, 0x02000000, _PAGE_IO);
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/* IDE functions */

static void __init
menf1_ide_init_hwif_ports (hw_regs_t *hw, unsigned long data_port,
		unsigned long ctrl_port, int *irq)
{
	unsigned long reg = data_port;
	int i = 8;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	if (ctrl_port)
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	else
		hw->io_ports[IDE_CONTROL_OFFSET] =
			hw->io_ports[IDE_DATA_OFFSET] + 0x206;

	if (irq != NULL)
		*irq = 0;
}

static int
menf1_ide_default_irq(unsigned long base)
{
	if (base == MENF1_IDE0_BASE_ADDR)
		return 14;
	else if (base == MENF1_IDE1_BASE_ADDR)
		return 15;
	else
		return 0;
}

static unsigned long
menf1_ide_default_io_base(int index)
{
	if (index == 0)
		return MENF1_IDE0_BASE_ADDR;
	else if (index == 1)
		return MENF1_IDE1_BASE_ADDR;
	else
		return 0;
}
#endif

TODC_ALLOC();

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	isa_io_base = MPC10X_MAPB_ISA_IO_BASE;
	isa_mem_base = MPC10X_MAPB_ISA_MEM_BASE;
	pci_dram_offset = MPC10X_MAPB_DRAM_OFFSET;

	ppc_md.setup_arch	= menf1_setup_arch;
	ppc_md.show_cpuinfo	= menf1_show_cpuinfo;
	ppc_md.init_IRQ		= menf1_init_IRQ;
	ppc_md.get_irq		= i8259_irq;

	ppc_md.find_end_of_memory = menf1_find_end_of_memory;
	ppc_md.setup_io_mappings = menf1_map_io;

	ppc_md.restart		= menf1_restart;
	ppc_md.power_off	= menf1_power_off;
	ppc_md.halt		= menf1_halt;

	TODC_INIT(TODC_TYPE_MK48T59,
		  MENF1_NVRAM_AS0,
		  MENF1_NVRAM_AS1,
		  MENF1_NVRAM_DATA,
		  7);

	ppc_md.time_init	= todc_time_init;
	ppc_md.get_rtc_time	= todc_get_rtc_time;
	ppc_md.set_rtc_time	= todc_set_rtc_time;
	ppc_md.calibrate_decr	= todc_calibrate_decr;

	ppc_md.nvram_read_val   = todc_m48txx_read_val;
	ppc_md.nvram_write_val  = todc_m48txx_write_val;

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
	ppc_ide_md.default_io_base = menf1_ide_default_io_base;
	ppc_ide_md.default_irq = menf1_ide_default_irq;
	ppc_ide_md.ide_init_hwif = menf1_ide_init_hwif_ports;
#endif
}
