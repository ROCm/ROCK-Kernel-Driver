/*
 * arch/ppc/platforms/pcore_setup.c
 *
 * Setup routines for Force PCORE boards
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
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/ide.h>
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

#include "pcore.h"

extern int pcore_find_bridges(void);
extern unsigned long loops_per_jiffy;

static int board_type;

/* Dummy variable to satisfy mpc10x_common.o */
void *OpenPIC_Addr;

static int
pcore_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: Force Computers\n");

	if (board_type == PCORE_TYPE_6750)
		seq_printf(m, "machine\t\t: PowerCore 6750\n");
	else /* PCORE_TYPE_680 */
		seq_printf(m, "machine\t\t: PowerCore 680\n");

	seq_printf(m, "L2\t\t: " );
	if (board_type == PCORE_TYPE_6750)
		switch (readb(PCORE_DCCR_REG) & PCORE_DCCR_L2_MASK)
		{
			case PCORE_DCCR_L2_0KB:
				seq_printf(m, "nocache");
				break;
			case PCORE_DCCR_L2_256KB:
				seq_printf(m, "256KB");
				break;
			case PCORE_DCCR_L2_1MB:
				seq_printf(m, "1MB");
				break;
			case PCORE_DCCR_L2_512KB:
				seq_printf(m, "512KB");
				break;
			default:
				seq_printf(m, "error");
				break;
		}
	else /* PCORE_TYPE_680 */
		switch (readb(PCORE_DCCR_REG) & PCORE_DCCR_L2_MASK)
		{
			case PCORE_DCCR_L2_2MB:
				seq_printf(m, "2MB");
				break;
			case PCORE_DCCR_L2_256KB:
				seq_printf(m, "reserved");
				break;
			case PCORE_DCCR_L2_1MB:
				seq_printf(m, "1MB");
				break;
			case PCORE_DCCR_L2_512KB:
				seq_printf(m, "512KB");
				break;
			default:
				seq_printf(m, "error");
				break;
		}

	seq_printf(m, "\n");

	return 0;
}

static void __init
pcore_setup_arch(void)
{
	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000/HZ;

	/* Lookup PCI host bridges */
	board_type = pcore_find_bridges();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
        else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA2;
#endif

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

		 printk("Force PCore port (C) 2001 MontaVista Software, Inc. (source@mvista.com)\n");
}

static void
pcore_restart(char *cmd)
{
	local_irq_disable();
	/* Hard reset */
	writeb(0x11, 0xfe000332);
	while(1);
}

static void
pcore_halt(void)
{
	local_irq_disable();
	/* Turn off user LEDs */
	writeb(0x00, 0xfe000300);
	while (1);
}

static void
pcore_power_off(void)
{
	pcore_halt();
}


static void __init
pcore_init_IRQ(void)
{
	int i;

	for ( i = 0 ; i < 16 ; i++ )
		irq_desc[i].handler = &i8259_pic;

	i8259_init(0);
}

/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space.
 */
static __inline__ void
pcore_set_bat(void)
{
	unsigned long   bat3u, bat3l;

	__asm__ __volatile__(
			" lis %0,0xf000\n \
			ori %1,%0,0x002a\n \
			ori %0,%0,0x1ffe\n \
			mtspr 0x21e,%0\n \
			mtspr 0x21f,%1\n \
			isync\n \
			sync "
			: "=r" (bat3u), "=r" (bat3l));
}

static unsigned long __init
pcore_find_end_of_memory(void)
{
	/* Cover I/O space with a BAT */
	/* yuck, better hope your ram size is a power of 2  -- paulus */
	pcore_set_bat();

	return mpc10x_get_mem_size(MPC10X_MEM_MAP_B);
}

static void __init
pcore_map_io(void)
{
	io_block_mapping(0xfe000000, 0xfe000000, 0x02000000, _PAGE_IO);
}

TODC_ALLOC();

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	isa_io_base = MPC10X_MAPB_ISA_IO_BASE;
	isa_mem_base = MPC10X_MAPB_ISA_MEM_BASE;
	pci_dram_offset = MPC10X_MAPB_DRAM_OFFSET;

	ppc_md.setup_arch	= pcore_setup_arch;
	ppc_md.show_cpuinfo	= pcore_show_cpuinfo;
	ppc_md.init_IRQ		= pcore_init_IRQ;
	ppc_md.get_irq		= i8259_irq;

	ppc_md.find_end_of_memory = pcore_find_end_of_memory;
	ppc_md.setup_io_mappings = pcore_map_io;

	ppc_md.restart		= pcore_restart;
	ppc_md.power_off	= pcore_power_off;
	ppc_md.halt		= pcore_halt;

	TODC_INIT(TODC_TYPE_MK48T59,
		  PCORE_NVRAM_AS0,
		  PCORE_NVRAM_AS1,
		  PCORE_NVRAM_DATA,
		  8);

	ppc_md.time_init	= todc_time_init;
	ppc_md.get_rtc_time	= todc_get_rtc_time;
	ppc_md.set_rtc_time	= todc_set_rtc_time;
	ppc_md.calibrate_decr	= todc_calibrate_decr;

	ppc_md.nvram_read_val	= todc_m48txx_read_val;
	ppc_md.nvram_write_val	= todc_m48txx_write_val;
}
