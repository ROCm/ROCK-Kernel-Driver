/*
 * arch/ppc/platforms/spruce_setup.c
 *
 * Board setup routines for IBM Spruce
 *
 * Authors: Johnnie Peters <jpeters@mvista.com>
 *          Matt Porter <mporter@mvista.com>
 *
 * 2001-2002 (c) MontaVista, Software, Inc.  This file is licensed under
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
#include <linux/seq_file.h>
#include <linux/ide.h>
#include <linux/root_dev.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_core.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <platforms/spruce.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/kgdb.h>

#include <syslib/cpc700.h>

extern void spruce_init_IRQ(void);
extern int spruce_get_irq(struct pt_regs *);
extern void spruce_setup_hose(void);

extern char cmd_line[];

/*
 * CPC700 PIC interrupt programming table
 *
 * First entry is the sensitivity (level/edge), second is the polarity.
 */
unsigned int cpc700_irq_assigns[32][2] = {
	{ 1, 1 },       /* IRQ  0: ECC Correctable Error - rising edge */
	{ 1, 1 },       /* IRQ  1: PCI Write Mem Range   - rising edge */
	{ 0, 1 },       /* IRQ  2: PCI Write Command Reg - active high */
	{ 0, 1 },       /* IRQ  3: UART 0                - active high */
	{ 0, 1 },       /* IRQ  4: UART 1                - active high */
	{ 0, 1 },       /* IRQ  5: ICC 0                 - active high */
	{ 0, 1 },       /* IRQ  6: ICC 1                 - active high */
	{ 0, 1 },       /* IRQ  7: GPT Compare 0         - active high */
	{ 0, 1 },       /* IRQ  8: GPT Compare 1         - active high */
	{ 0, 1 },       /* IRQ  9: GPT Compare 2         - active high */
	{ 0, 1 },       /* IRQ 10: GPT Compare 3         - active high */
	{ 0, 1 },       /* IRQ 11: GPT Compare 4         - active high */
	{ 0, 1 },       /* IRQ 12: GPT Capture 0         - active high */
	{ 0, 1 },       /* IRQ 13: GPT Capture 1         - active high */
	{ 0, 1 },       /* IRQ 14: GPT Capture 2         - active high */
	{ 0, 1 },       /* IRQ 15: GPT Capture 3         - active high */
	{ 0, 1 },       /* IRQ 16: GPT Capture 4         - active high */
	{ 0, 0 },       /* IRQ 17: Reserved */
	{ 0, 0 },       /* IRQ 18: Reserved */
	{ 0, 0 },       /* IRQ 19: Reserved */
	{ 0, 1 },       /* IRQ 20: FPGA EXT_IRQ0         - active high */
	{ 1, 1 },       /* IRQ 21: Mouse                 - rising edge */
	{ 1, 1 },       /* IRQ 22: Keyboard              - rising edge */
	{ 0, 0 },       /* IRQ 23: PCI Slot 3            - active low */
	{ 0, 0 },       /* IRQ 24: PCI Slot 2            - active low */
	{ 0, 0 },       /* IRQ 25: PCI Slot 1            - active low */
	{ 0, 0 },       /* IRQ 26: PCI Slot 0            - active low */
};

static void __init
spruce_calibrate_decr(void)
{
	int freq, divisor = 4;

	/* determine processor bus speed */
	freq = SPRUCE_BUS_SPEED;
	tb_ticks_per_jiffy = freq / HZ / divisor;
	tb_to_us = mulhwu_scale_factor(freq/divisor, 1000000);
}

static int
spruce_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: IBM\n");
	seq_printf(m, "machine\t\t: Spruce\n");

	return 0;
}

static void __init
spruce_early_serial_map(void)
{
	u32 uart_clk;
	struct uart_port serial_req;

	if (SPRUCE_UARTCLK_IS_33M(readb(SPRUCE_FPGA_REG_A)))
		uart_clk = SPRUCE_BAUD_33M * 16;
	else
		uart_clk = SPRUCE_BAUD_30M * 16;

	/* Setup serial port access */
	memset(&serial_req, 0, sizeof(serial_req));
	serial_req.uartclk = uart_clk;
	serial_req.irq = UART0_INT;
	serial_req.flags = ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST;
	serial_req.iotype = SERIAL_IO_MEM;
	serial_req.membase = (u_char *)UART0_IO_BASE;
	serial_req.regshift = 0;

#if defined(CONFIG_KGDB) || defined(CONFIG_SERIAL_TEXT_DEBUG)
	gen550_init(0, &serial_req);
#endif
#ifdef CONFIG_SERIAL_8250
	if (early_serial_setup(&serial_req) != 0)
		printk("Early serial init of port 0 failed\n");
#endif

	/* Assume early_serial_setup() doesn't modify serial_req */
	serial_req.line = 1;
	serial_req.irq = UART1_INT;
	serial_req.membase = (u_char *)UART1_IO_BASE;

#if defined(CONFIG_KGDB) || defined(CONFIG_SERIAL_TEXT_DEBUG)
	gen550_init(1, &serial_req);
#endif
#ifdef CONFIG_SERIAL_8250
	if (early_serial_setup(&serial_req) != 0)
		printk("Early serial init of port 1 failed\n");
#endif
}

TODC_ALLOC();

static void __init
spruce_setup_arch(void)
{
	/* Setup TODC access */
	TODC_INIT(TODC_TYPE_DS1643, 0, 0, SPRUCE_RTC_BASE_ADDR, 8);

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000 / HZ;

	/* Setup PCI host bridge */
	spruce_setup_hose();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA1;
#endif

#ifdef CONFIG_VT
	conswitchp = &dummy_con;
#endif


	/* Identify the system */
	printk(KERN_INFO "System Identification: IBM Spruce\n");
	printk(KERN_INFO "Port by MontaVista Software, Inc. (source@mvista.com)\n");
}

static void
spruce_restart(char *cmd)
{
	local_irq_disable();

	/* SRR0 has system reset vector, SRR1 has default MSR value */
	/* rfi restores MSR from SRR1 and sets the PC to the SRR0 value */
	__asm__ __volatile__
	("\n\
	lis	3,0xfff0	\n\
	ori	3,3,0x0100	\n\
	mtspr	26,3		\n\
	li	3,0		\n\
	mtspr	27,3		\n\
	rfi			\n\
	");
	for(;;);
}

static void
spruce_power_off(void)
{
	for(;;);
}

static void
spruce_halt(void)
{
	spruce_restart(NULL);
}

static void __init
spruce_map_io(void)
{
	io_block_mapping(SPRUCE_PCI_IO_BASE, SPRUCE_PCI_PHY_IO_BASE,
			 0x08000000, _PAGE_IO);
}

/*
 * Set BAT 3 to map 0xf8000000 to end of physical memory space 1-to-1.
 */
static __inline__ void
spruce_set_bat(void)
{
	mb();
	mtspr(DBAT1U, 0xf8000ffe);
	mtspr(DBAT1L, 0xf800002a);
	mb();
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	/* Map in board regs, etc. */
	spruce_set_bat();

	isa_io_base = SPRUCE_ISA_IO_BASE;
	pci_dram_offset = SPRUCE_PCI_SYS_MEM_BASE;

	ppc_md.setup_arch = spruce_setup_arch;
	ppc_md.show_cpuinfo = spruce_show_cpuinfo;
	ppc_md.init_IRQ = cpc700_init_IRQ;
	ppc_md.get_irq = cpc700_get_irq;

	ppc_md.setup_io_mappings = spruce_map_io;

	ppc_md.restart = spruce_restart;
	ppc_md.power_off = spruce_power_off;
	ppc_md.halt = spruce_halt;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = spruce_calibrate_decr;

	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;

	spruce_early_serial_map();

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = gen550_progress;
#endif /* CONFIG_SERIAL_TEXT_DEBUG */
#ifdef CONFIG_KGDB
	ppc_md.kgdb_map_scc = gen550_kgdb_map_scc;
#endif
}
