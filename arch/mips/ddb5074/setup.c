/*
 *  arch/mips/ddb5074/setup.c -- NEC DDB Vrc-5074 setup routines
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 *
 *  $Id: setup.c,v 1.1 2000/01/26 00:07:44 ralf Exp $
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
#include <asm/ddb5074.h>


#ifdef CONFIG_REMOTE_DEBUG
extern void rs_kgdb_hook(int);
extern void breakpoint(void);
#endif

#if defined(CONFIG_SERIAL_CONSOLE)
extern void console_setup(char *);
#endif

extern struct ide_ops std_ide_ops;
extern struct rtc_ops ddb_rtc_ops;

static void (*back_to_prom)(void) = (void (*)(void))0xbfc00000;

static void ddb_machine_restart(char *command)
{
    u32 t;

    /* PCI cold reset */
    t = nile4_in32(NILE4_PCICTRL+4);
    t |= 0x40000000;
    nile4_out32(NILE4_PCICTRL+4, t);
    /* CPU cold reset */
    t = nile4_in32(NILE4_CPUSTAT);
    t |= 1;
    nile4_out32(NILE4_CPUSTAT, t);
    /* Call the PROM */
    back_to_prom();
}

static void ddb_machine_halt(void)
{
    printk("DDB Vrc-5074 halted.\n");
    do {} while (1);
}

static void ddb_machine_power_off(void)
{
    printk("DDB Vrc-5074 halted. Please turn off the power.\n");
    do {} while (1);
}

extern void ddb_irq_setup(void);

void (*board_time_init)(struct irqaction *irq);


static void __init ddb_time_init(struct irqaction *irq)
{
    /* set the clock to 1 Hz */
    nile4_out32(NILE4_T2CTRL, 1000000);
    /* enable the General-Purpose Timer */
    nile4_out32(NILE4_T2CTRL+4, 0x00000001);
    /* reset timer */
    nile4_out32(NILE4_T2CNTR, 0);
    /* enable interrupt */
    nile4_enable_irq(NILE4_INT_GPT);
    i8259_setup_irq(nile4_to_irq(NILE4_INT_GPT), irq);
    set_cp0_status(ST0_IM, IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4);
}

void __init ddb_setup(void)
{
    extern int panic_timeout;

    irq_setup = ddb_irq_setup;
    mips_io_port_base = NILE4_PCI_IO_BASE;
    isa_slot_offset = NILE4_PCI_MEM_BASE;
    request_region(0x00, 0x20, "dma1");
    request_region(0x40, 0x20, "timer");
    request_region(0x70, 0x10, "rtc");
    request_region(0x80, 0x10, "dma page reg");
    request_region(0xc0, 0x20, "dma2");
    board_time_init = ddb_time_init;

    _machine_restart = ddb_machine_restart;
    _machine_halt = ddb_machine_halt;
    _machine_power_off = ddb_machine_power_off;

#ifdef CONFIG_BLK_DEV_IDE
    ide_ops = &std_ide_ops;
#endif
    rtc_ops = &ddb_rtc_ops;

    /* Reboot on panic */
    panic_timeout = 180;
}

int __init page_is_ram(unsigned long pagenr)
{
    return 1;
}


#define USE_NILE4_SERIAL	0

#if USE_NILE4_SERIAL
#define ns16550_in(reg)		nile4_in8((reg)*8)
#define ns16550_out(reg, val)	nile4_out8((reg)*8, (val))
#else
#define NS16550_BASE		(NILE4_PCI_IO_BASE+0x03f8)
static inline u8 ns16550_in(u32 reg)
{
    return *(volatile u8 *)(NS16550_BASE+reg);
}

static inline void ns16550_out(u32 reg, u8 val)
{
    *(volatile u8 *)(NS16550_BASE+reg) = val;
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

void ddb5074_led_hex(int hex)
{
    outb(hex, 0x80);
}


    /*
     *  LEDs D2 and D3, connected to the GPIO pins of the PMU in the ALi M1543
     */

struct pci_dev *pci_pmu = NULL;

void ddb5074_led_d2(int on)
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

void ddb5074_led_d3(int on)
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

