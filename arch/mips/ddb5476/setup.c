/*
 *  arch/mips/ddb5476/setup.c -- NEC DDB Vrc-5476 setup routines
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kbd_ll.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/console.h>
#include <linux/sched.h>
#include <linux/mc146818rtc.h>
#include <linux/pc_keyb.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/addrspace.h>
#include <asm/bcache.h>
#include <asm/keyboard.h>
#include <asm/irq.h>
#include <asm/reboot.h>
#include <asm/gdb-stub.h>
#include <asm/nile4.h>
#include <asm/time.h>


#ifdef CONFIG_REMOTE_DEBUG
extern void rs_kgdb_hook(int);
extern void breakpoint(void);
#endif

#if defined(CONFIG_SERIAL_CONSOLE)
extern void console_setup(char *);
#endif

extern struct ide_ops std_ide_ops;
extern struct rtc_ops ddb_rtc_ops;
extern struct kbd_ops std_kbd_ops;

static void (*back_to_prom) (void) = (void (*)(void)) 0xbfc00000;

static void ddb_machine_restart(char *command)
{
	u32 t;

	/* PCI cold reset */
	t = nile4_in32(NILE4_PCICTRL + 4);
	t |= 0x40000000;
	nile4_out32(NILE4_PCICTRL + 4, t);
	/* CPU cold reset */
	t = nile4_in32(NILE4_CPUSTAT);
	t |= 1;
	nile4_out32(NILE4_CPUSTAT, t);
	/* Call the PROM */
	back_to_prom();
}

static void ddb_machine_halt(void)
{
	printk("DDB Vrc-5476 halted.\n");
	while (1);
}

static void ddb_machine_power_off(void)
{
	printk("DDB Vrc-5476 halted. Please turn off the power.\n");
	while (1);
}

extern void ddb_irq_setup(void);

static void __init ddb_time_init(struct irqaction *irq)
{
	printk("ddb_time_init invoked.\n");
	mips_counter_frequency = 83000000;
}

static void __init ddb_timer_setup(struct irqaction *irq)
{
	unsigned int count;

	/* we are using the cpu counter for timer interrupts */
	i8259_setup_irq(0, irq);
	set_cp0_status(IE_IRQ5);

	/* to generate the first timer interrupt */
	count = read_32bit_cp0_register(CP0_COUNT);
	write_32bit_cp0_register(CP0_COMPARE, count + 1000);

#if 0		/* the old way to do timer interrupt */
	/* set the clock to 100 Hz */
	nile4_out32(NILE4_T2CTRL, 830000);
	/* enable the General-Purpose Timer */
	nile4_out32(NILE4_T2CTRL + 4, 0x00000001);
	/* reset timer */
	nile4_out32(NILE4_T2CNTR, 0);
	/* enable interrupt */
	nile4_enable_irq(NILE4_INT_GPT);
	i8259_setup_irq(nile4_to_irq(NILE4_INT_GPT), irq);
#endif
}

static struct {
	struct resource dma1;
	struct resource pic1;
	struct resource timer;
	struct resource rtc;
	struct resource dma_page_reg;
	struct resource pic2;
	struct resource dma2;
} ddb5476_ioport = {
	{
	"dma1", 0x00, 0x1f, IORESOURCE_BUSY}, {
	"pic1", 0x20, 0x3f, IORESOURCE_BUSY}, {
	"timer", 0x40, 0x5f, IORESOURCE_BUSY}, {
	"rtc", 0x70, 0x7f, IORESOURCE_BUSY}, {
	"dma page reg", 0x80, 0x8f, IORESOURCE_BUSY}, {
	"pic2", 0xa0, 0xbf, IORESOURCE_BUSY}, {
	"dma2", 0xc0, 0xdf, IORESOURCE_BUSY}
};

static struct {
	struct resource nile4;
} ddb5476_iomem = {
	{ "Nile 4", NILE4_BASE, NILE4_BASE + NILE4_SIZE - 1, IORESOURCE_BUSY}
};

void __init ddb_setup(void)
{
	extern int panic_timeout;

	irq_setup = ddb_irq_setup;
	mips_io_port_base = NILE4_PCI_IO_BASE;
	isa_slot_offset = NILE4_PCI_MEM_BASE;

	board_time_init = ddb_time_init;
	board_timer_setup = ddb_timer_setup;

	_machine_restart = ddb_machine_restart;
	_machine_halt = ddb_machine_halt;
	_machine_power_off = ddb_machine_power_off;

	/* request io port/mem resources  */
	if (request_resource(&ioport_resource, &ddb5476_ioport.dma1) ||
	    request_resource(&ioport_resource, &ddb5476_ioport.pic1) ||
	    request_resource(&ioport_resource, &ddb5476_ioport.timer) ||
	    request_resource(&ioport_resource, &ddb5476_ioport.rtc) ||
	    request_resource(&ioport_resource,
			     &ddb5476_ioport.dma_page_reg)
	    || request_resource(&ioport_resource, &ddb5476_ioport.pic2)
	    || request_resource(&ioport_resource, &ddb5476_ioport.dma2)
	    || request_resource(&iomem_resource, &ddb5476_iomem.nile4)) {
		printk
		    ("ddb_setup - requesting oo port resources failed.\n");
		for (;;);
	}
#ifdef CONFIG_BLK_DEV_IDE
	ide_ops = &std_ide_ops;
#endif
	rtc_ops = &ddb_rtc_ops;

#ifdef CONFIG_PC_KEYB
	kbd_ops = &std_kbd_ops;
#endif

	/* Reboot on panic */
	panic_timeout = 180;

	/* [jsun] we need to set BAR0 so that SDRAM 0 appears at 0x0 in PCI */
	/* *(long*)0xbfa00218 = 0x8; */

#ifdef CONFIG_FB
	conswitchp = &dummy_con;
#endif


	/* board initialization stuff - non-fundamental, but need to be set
	 * before kernel runs */

	/* setup I/O space */
	nile4_set_pdar(NILE4_PCIW0,
		       PHYSADDR(NILE4_PCI_IO_BASE), 0x02000000, 32, 0, 0);
	nile4_set_pmr(NILE4_PCIINIT0, NILE4_PCICMD_IO, 0);

	/* map config space to 0xa8000000, 128MB */
	nile4_set_pdar(NILE4_PCIW1,
		       PHYSADDR(NILE4_PCI_CFG_BASE), 0x08000000, 32, 0, 0);
	nile4_set_pmr(NILE4_PCIINIT1, NILE4_PCICMD_CFG, 0x0);

	/* ----- M1543 PCI setup ------ */

	/* we know M1543 PCI-ISA controller is at addr:18 */
	/* xxxx1010 makes USB at addr:13 and PMU at addr:14 */
	*(volatile unsigned char *) 0xa8040072 &= 0xf0;
	*(volatile unsigned char *) 0xa8040072 |= 0xa;

	/* setup USB interrupt to IRQ 9, (bit 0:3 - 0001)
	 * no IOCHRDY signal, (bit 7 - 1)
	 * M1543C & M7101 VID and Subsys Device ID are read-only (bit 6 - 1)
	 * Bypass USB Master INTAJ level to edge conversion (bit 4 - 0)
	 */
	*(unsigned char *) 0xa8040074 = 0xc1;

	/* setup PMU(SCI to IRQ 10 (bit 0:3 - 0011)
	 * SCI routing to IRQ 13 disabled (bit 7 - 1)
	 * SCI interrupt level to edge conversion bypassed (bit 4 - 0)
	 */
	*(unsigned char *) 0xa8040076 = 0x83;

	/* setup IDE controller
	 * enable IDE controller (bit 6 - 1)
	 * IDE IDSEL to be addr:24 (bit 4:5 - 11)
	 * no IDE ATA Secondary Bus Signal Pad Control (bit 3 - 0)
	 * no IDE ATA Primary Bus Signal Pad Control (bit 2 - 0)
	 * primary IRQ is 14, secondary is 15 (bit 1:0 - 01
	 */
	// *(unsigned char*)0xa8040058 = 0x71;
	// *(unsigned char*)0xa8040058 = 0x79;
	// *(unsigned char*)0xa8040058 = 0x74;              // use SIRQ, primary tri-state
	*(unsigned char *) 0xa8040058 = 0x75;	// primary tri-state

#if 0
	/* this is not necessary if M5229 does not use SIRQ */
	*(unsigned char *) 0xa8040044 = 0x0d;	// primary to IRQ 14
	*(unsigned char *) 0xa8040075 = 0x0d;	// secondary to IRQ 14
#endif

	/* enable IDE in the M5229 config register 0x50 (bit 0 - 1) */
	/* M5229 IDSEL is addr:24; see above setting */
	*(unsigned char *) 0xa9000050 |= 0x1;

	/* enable bus master (bit 2)  and IO decoding  (bit 0) */
	*(unsigned char *) 0xa9000004 |= 0x5;

	/* enable native, copied from arch/ppc/k2boot/head.S */
	/* TODO - need volatile, need to be portable */
	*(unsigned char *) 0xa9000009 = 0xff;

	/* ----- end of M1543 PCI setup ------ */

	/* ----- reset on-board ether chip  ------ */
	*((volatile u32 *) 0xa8020004) |= 1;	/* decode I/O */
	*((volatile u32 *) 0xa8020010) = 0;	/* set BAR address */

	/* send reset command */
	*((volatile u32 *) 0xa6000000) = 1;	/* do a soft reset */

	/* disable ether chip */
	*((volatile u32 *) 0xa8020004) = 0;	/* disable any decoding */

	/* put it into sleep */
	*((volatile u32 *) 0xa8020040) = 0x80000000;

	/* ----- end of reset on-board ether chip  ------ */

	/* ----- set pci window 1 to pci memory space -------- */
	nile4_set_pdar(NILE4_PCIW1,
		       PHYSADDR(NILE4_PCI_MEM_BASE), 0x08000000, 32, 0, 0);
	// nile4_set_pmr(NILE4_PCIINIT1, NILE4_PCICMD_MEM, 0);
	nile4_set_pmr(NILE4_PCIINIT1, NILE4_PCICMD_MEM, 0x08000000);

}

#define USE_NILE4_SERIAL	0

#if USE_NILE4_SERIAL
#define ns16550_in(reg)		nile4_in8((reg)*8)
#define ns16550_out(reg, val)	nile4_out8((reg)*8, (val))
#else
#define NS16550_BASE		(NILE4_PCI_IO_BASE+0x03f8)
static inline u8 ns16550_in(u32 reg)
{
	return *(volatile u8 *) (NS16550_BASE + reg);
}

static inline void ns16550_out(u32 reg, u8 val)
{
	*(volatile u8 *) (NS16550_BASE + reg) = val;
}
#endif

#define NS16550_RBR		0
#define NS16550_THR		0
#define NS16550_DLL		0
#define NS16550_IER		1
#define NS16550_DLM		1
#define NS16550_FCR		2
#define NS16550_IIR		2
#define NS16550_LCR		3
#define NS16550_MCR		4
#define NS16550_LSR		5
#define NS16550_MSR		6
#define NS16550_SCR		7

#define NS16550_LSR_DR		0x01	/* Data ready */
#define NS16550_LSR_OE		0x02	/* Overrun */
#define NS16550_LSR_PE		0x04	/* Parity error */
#define NS16550_LSR_FE		0x08	/* Framing error */
#define NS16550_LSR_BI		0x10	/* Break */
#define NS16550_LSR_THRE	0x20	/* Xmit holding register empty */
#define NS16550_LSR_TEMT	0x40	/* Xmitter empty */
#define NS16550_LSR_ERR		0x80	/* Error */


void _serinit(void)
{
#if USE_NILE4_SERIAL
	ns16550_out(NS16550_LCR, 0x80);
	ns16550_out(NS16550_DLM, 0x00);
	ns16550_out(NS16550_DLL, 0x36);	/* 9600 baud */
	ns16550_out(NS16550_LCR, 0x00);
	ns16550_out(NS16550_LCR, 0x03);
	ns16550_out(NS16550_FCR, 0x47);
#else
	/* done by PMON */
#endif
}

void _putc(char c)
{
	while (!(ns16550_in(NS16550_LSR) & NS16550_LSR_THRE));
	ns16550_out(NS16550_THR, c);
	if (c == '\n') {
		while (!(ns16550_in(NS16550_LSR) & NS16550_LSR_THRE));
		ns16550_out(NS16550_THR, '\r');
	}
}

void _puts(const char *s)
{
	char c;

	while ((c = *s++))
		_putc(c);
}

char _getc(void)
{
	while (!(ns16550_in(NS16550_LSR) & NS16550_LSR_DR));

	return ns16550_in(NS16550_RBR);
}

int _testc(void)
{
	return (ns16550_in(NS16550_LSR) & NS16550_LSR_DR) != 0;
}


/*
 *  Hexadecimal 7-segment LED
 */
void ddb5476_led_hex(int hex)
{
	outb(hex, 0x80);
}


/*
 *  LEDs D2 and D3, connected to the GPIO pins of the PMU in the ALi M1543
 */
struct pci_dev *pci_pmu = NULL;

void ddb5476_led_d2(int on)
{
	u8 t;

	if (pci_pmu) {
		pci_read_config_byte(pci_pmu, 0x7e, &t);
		if (on)
			t &= 0x7f;
		else
			t |= 0x80;
		pci_write_config_byte(pci_pmu, 0x7e, t);
	}
}

void ddb5476_led_d3(int on)
{
	u8 t;

	if (pci_pmu) {
		pci_read_config_byte(pci_pmu, 0x7e, &t);
		if (on)
			t &= 0xbf;
		else
			t |= 0x40;
		pci_write_config_byte(pci_pmu, 0x7e, t);
	}
}
