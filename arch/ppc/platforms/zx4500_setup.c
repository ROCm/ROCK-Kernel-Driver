/*
 * arch/ppc/platforms/zx4500_setup.c
 *
 * Board setup routines for Znyx ZX4500 family of cPCI boards.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2000-2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * This file adds support for the Znyx ZX4500 series of cPCI boards.
 * These boards have an 8240, UART on the processor bus, a PPMC slot (for now
 * the card in this slot can _not_ be a monarch), Broadcom BCM5600, and an
 * Intel 21554 bridge.
 *
 * Currently, this port assumes that the 8240 is the master and performs PCI
 * arbitration, etc.  It is also assumed that the 8240 is wired to come up
 * using memory MAP B (CHRP map).
 *
 * Note: This board port will not work properly as it is.  You must apply the
 *	 patch that is at ftp://ftp.mvista.com/pub/Area51/zx4500/zx_patch_2_5
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
#include <linux/root_dev.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/open_pic.h>
#include <asm/mpc10x.h>
#include <asm/pci-bridge.h>
#include <asm/bootinfo.h>

#include "zx4500.h"

static u_char zx4500_openpic_initsenses[] __initdata = {
	0,	/* 0-15 are not used on an 8240 EPIC */
	0,	/* 1 */
	0,	/* 2 */
	0,	/* 3 */
	0,	/* 4 */
	0,	/* 5 */
	0,	/* 6 */
	0,	/* 7 */
	0,	/* 8 */
	0,	/* 9 */
	0,	/* 10 */
	0,	/* 11 */
	0,	/* 12 */
	0,	/* 13 */
	0,	/* 14 */
	0,	/* 15 */
	1,	/* 16: EPIC IRQ 0: Active Low -- PMC #INTA & #INTC */
	1,	/* 17: EPIC IRQ 1: Active Low -- UART */
	1,	/* 18: EPIC IRQ 2: Active Low -- BCM5600 #INTA */
	1,	/* 19: EPIC IRQ 3: Active Low -- 21554 #SINTA */
	1,	/* 20: EPIC IRQ 4: Active Low -- PMC #INTB & #INTD */
};


static void __init
zx4500_setup_arch(void)
{
	char		boot_string[ZX4500_BOOT_STRING_LEN + 1];
	char		*boot_arg;
	extern char	cmd_line[];


	loops_per_jiffy = 50000000 / HZ;

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#if	defined(CONFIG_ROOT_NFS)
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA1;
#endif

	/* Get boot string from flash */
	strlcpy(boot_string,
		(char *)ZX4500_BOOT_STRING_ADDR,
		sizeof(boot_string));
	boot_string[ZX4500_BOOT_STRING_LEN] = '\0';

	/* Can be delimited by 0xff */
	boot_arg = strchr(boot_string, 0xff);

	if (boot_arg != NULL) {
		*boot_arg = '\0';
	}

	/* First 3 chars must be 'dev'.  If not, ignore. */
	if (!strncmp(boot_string, "dev", 3)) {
		/* skip 'dev?' and any blanks after it */
		boot_arg = strchr(boot_string, ' ');

		if (boot_arg != NULL) {
			while (*boot_arg == ' ') boot_arg++;
			strcat(cmd_line, " ");
			strcat(cmd_line, boot_arg);
		}
	}

	/* nothing but serial consoles... */
	printk("Znyx ZX4500 Series High Performance Switch\n");
	printk("ZX4500 port (C) 2000, 2001 MontaVista Software, Inc. (source@mvista.com)\n");

	/* Lookup PCI host bridge */
	zx4500_find_bridges();

	printk("ZX4500 Board ID: 0x%x, Revision #: 0x%x\n",
		in_8((volatile u_char *)ZX4500_CPLD_BOARD_ID),
		in_8((volatile u_char *)ZX4500_CPLD_REV));

	return;
}

static ulong __init
zx4500_find_end_of_memory(void)
{
	return mpc10x_get_mem_size(MPC10X_MEM_MAP_B);
}

static void __init
zx4500_map_io(void)
{
	io_block_mapping(0xfe000000, 0xfe000000, 0x02000000, _PAGE_IO);
}

/*
 * Enable interrupts routed thru CPLD to reach the 8240's EPIC.
 * Need to enable all 4 PMC intrs, BCM INTA, and 21554 SINTA to 8240.
 * UART intrs routed directly to 8240 (not thru CPLD).
 */
static void __init
zx4500_enable_cpld_intrs(void)
{
	u_char	sysctl;

	sysctl = in_8((volatile u_char *)ZX4500_CPLD_SYSCTL);
	sysctl |= (ZX4500_CPLD_SYSCTL_PMC |
		   ZX4500_CPLD_SYSCTL_BCM |
		   ZX4500_CPLD_SYSCTL_SINTA);
	out_8((volatile u_char *)ZX4500_CPLD_SYSCTL, sysctl);

	return;
}

static void __init
zx4500_init_IRQ(void)
{
	OpenPIC_InitSenses = zx4500_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(zx4500_openpic_initsenses);

	openpic_init(1, 0, NULL, -1);

	zx4500_enable_cpld_intrs(); /* Allow CPLD to route intrs to 8240 */

	return;
}

static void
zx4500_restart(char *cmd)
{
	local_irq_disable();

	out_8((volatile u_char *)ZX4500_CPLD_RESET, ZX4500_CPLD_RESET_XBUS);
	for (;;);

	panic("Restart failed.\n");
	/* NOTREACHED */
}

static void
zx4500_power_off(void)
{
	local_irq_disable();
	for(;;);  /* No way to shut power off with software */
	/* NOTREACHED */
}

static void
zx4500_halt(void)
{
	zx4500_power_off();
	/* NOTREACHED */
}

static int
zx4500_get_bus_speed(void)
{
	int bus_speed;

	bus_speed = 100000000;

	return bus_speed;
}

static int
zx4500_show_cpuinfo(struct seq_file *m)
{
	uint pvid;

	seq_printf(m, "vendor\t\t: Znyx\n");
	seq_printf(m, "machine\t\t: ZX4500\n");
	seq_printf(m, "processor\t: PVID: 0x%x, vendor: %s\n",
			pvid, (pvid & (1<<15) ? "IBM" : "Motorola"));
	seq_printf(m, "bus speed\t: %dMhz\n",
			zx4500_get_bus_speed()/1000000);

	return 0;
}

static void __init
zx4500_calibrate_decr(void)
{
	ulong freq;

	freq = zx4500_get_bus_speed() / 4;

	printk("time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       freq/1000000, freq%1000000);

	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	return;
}

/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space 1-1.
 */
static __inline__ void
zx4500_set_bat(void)
{
	unsigned long   bat3u, bat3l;
	static int	mapping_set = 0;

	if (!mapping_set) {

		__asm__ __volatile__(
		" lis %0,0xf800\n \
		  ori %1,%0,0x002a\n \
		  ori %0,%0,0x0ffe\n \
		  mtspr 0x21e,%0\n \
		  mtspr 0x21f,%1\n \
		  isync\n \
		  sync "
		  : "=r" (bat3u), "=r" (bat3l));

		mapping_set = 1;
	}

	return;
}

#ifdef	CONFIG_SERIAL_TEXT_DEBUG
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <asm/serial.h>

static struct serial_state rs_table[RS_TABLE_SIZE] = {
	SERIAL_PORT_DFNS	/* Defined in <asm/serial.h> */
};

void
zx4500_progress(char *s, unsigned short hex)
{
	volatile char c;
	volatile unsigned long com_port;
	u16 shift;

	com_port = rs_table[0].port;
	shift = rs_table[0].iomem_reg_shift;

	while ((c = *s++) != 0) {
		while ((*((volatile unsigned char *)com_port +
				(UART_LSR << shift)) & UART_LSR_THRE) == 0)
		                ;
	        *(volatile unsigned char *)com_port = c;

		if (c == '\n') {
			while ((*((volatile unsigned char *)com_port +
				(UART_LSR << shift)) & UART_LSR_THRE) == 0)
					;
	        	*(volatile unsigned char *)com_port = '\r';
		}
	}
}
#endif	/* CONFIG_SERIAL_TEXT_DEBUG */

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	/* Map in board registers, etc. */
	zx4500_set_bat();

	isa_io_base = MPC10X_MAPB_ISA_IO_BASE;
	isa_mem_base = MPC10X_MAPB_ISA_MEM_BASE;
	pci_dram_offset = MPC10X_MAPB_DRAM_OFFSET;

	ppc_md.setup_arch = zx4500_setup_arch;
	ppc_md.show_cpuinfo = zx4500_show_cpuinfo;
	ppc_md.irq_canonicalize = NULL;
	ppc_md.init_IRQ = zx4500_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;
	ppc_md.init = NULL;

	ppc_md.restart = zx4500_restart;
	ppc_md.power_off = zx4500_power_off;
	ppc_md.halt = zx4500_halt;

	ppc_md.find_end_of_memory = zx4500_find_end_of_memory;
	ppc_md.setup_io_mappings = zx4500_map_io;

	ppc_md.calibrate_decr = zx4500_calibrate_decr;

	ppc_md.heartbeat = NULL;
	ppc_md.heartbeat_reset = 0;
	ppc_md.heartbeat_count = 0;

#ifdef	CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = zx4500_progress;
#else	/* !CONFIG_SERIAL_TEXT_DEBUG */
	ppc_md.progress = NULL;
#endif	/* CONFIG_SERIAL_TEXT_DEBUG */

	return;
}
