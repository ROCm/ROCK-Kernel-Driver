/*
 *  arch/mips/ddb5074/irq.c -- NEC DDB Vrc-5074 interrupt routines
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 *
 *  $Id: irq.c,v 1.1 2000/01/26 00:07:44 ralf Exp $
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ptrace.h>
#include <asm/nile4.h>
#include <asm/ddb5074.h>


extern void __init i8259_init(void);
extern void i8259_disable_irq(unsigned int irq_nr);
extern void i8259_enable_irq(unsigned int irq_nr);

extern asmlinkage void ddbIRQ(void);
extern asmlinkage void i8259_do_irq(int irq, struct pt_regs *regs);
extern asmlinkage void do_IRQ(int irq, struct pt_regs * regs);


void no_action(int cpl, void *dev_id, struct pt_regs *regs)
{
}


#define M1543_PNP_CONFIG	0x03f0	/* PnP Config Port */
#define M1543_PNP_INDEX		0x03f0	/* PnP Index Port */
#define M1543_PNP_DATA		0x03f1	/* PnP Data Port */

#define M1543_PNP_ALT_CONFIG	0x0370	/* Alternative PnP Config Port */
#define M1543_PNP_ALT_INDEX	0x0370	/* Alternative PnP Index Port */
#define M1543_PNP_ALT_DATA	0x0371	/* Alternative PnP Data Port */

#define M1543_INT1_MASTER_CTRL	0x0020	/* INT_1 (master) Control Register */
#define M1543_INT1_MASTER_MASK	0x0021	/* INT_1 (master) Mask Register */

#define M1543_INT1_SLAVE_CTRL	0x00a0	/* INT_1 (slave) Control Register */
#define M1543_INT1_SLAVE_MASK	0x00a1	/* INT_1 (slave) Mask Register */

#define M1543_INT1_MASTER_ELCR	0x04d0	/* INT_1 (master) Edge/Level Control */
#define M1543_INT1_SLAVE_ELCR	0x04d1	/* INT_1 (slave) Edge/Level Control */


static void m1543_irq_setup(void)
{
    /*
     *  The ALI M1543 has 13 interrupt inputs, IRQ1..IRQ13.  Not all
     *  the possible IO sources in the M1543 are in use by us.  We will
     *  use the following mapping:
     *
     *	    IRQ1  - keyboard (default set by M1543)
     *	    IRQ3  - reserved for UART B (default set by M1543) (note that
     *		    the schematics for the DDB Vrc-5074 board seem to 
     *		    indicate that IRQ3 is connected to the DS1386 
     *		    watchdog timer interrupt output so we might have 
     *		    a conflict)
     *	    IRQ4  - reserved for UART A (default set by M1543)
     *	    IRQ5  - parallel (default set by M1543)
     *	    IRQ8  - DS1386 time of day (RTC) interrupt
     *	    IRQ12 - mouse
     */
 
    /*
     *  Assing mouse interrupt to IRQ12 
     */

    /* Enter configuration mode */
    outb(0x51, M1543_PNP_CONFIG);
    outb(0x23, M1543_PNP_CONFIG);

    /* Select logical device 7 (Keyboard) */
    outb(0x07, M1543_PNP_INDEX);
    outb(0x07, M1543_PNP_DATA);

    /* Select IRQ12 */
    outb(0x72, M1543_PNP_INDEX);
    outb(0x0c, M1543_PNP_DATA);

    /* Leave configration mode */
    outb(0xbb, M1543_PNP_CONFIG);


    /* Initialize the 8259 PIC in the M1543 */
    i8259_init();

    /* Enable the interrupt cascade */
    nile4_enable_irq(NILE4_INT_INTE);

    request_region(M1543_PNP_CONFIG, 2, "M1543 config");
    request_region(M1543_INT1_MASTER_ELCR, 2, "pic ELCR");
}

static void nile4_irq_setup(void)
{
    int i;

    /* Map all interrupts to CPU int #0 */
    nile4_map_irq_all(0);

    /* PCI INTA#-E# must be level triggered */
    nile4_set_pci_irq_level_or_edge(0, 1);
    nile4_set_pci_irq_level_or_edge(1, 1);
    nile4_set_pci_irq_level_or_edge(2, 1);
    nile4_set_pci_irq_level_or_edge(3, 1);
    nile4_set_pci_irq_level_or_edge(4, 1);

    /* PCI INTA#-D# must be active low, INTE# must be active high */
    nile4_set_pci_irq_polarity(0, 0);
    nile4_set_pci_irq_polarity(1, 0);
    nile4_set_pci_irq_polarity(2, 0);
    nile4_set_pci_irq_polarity(3, 0);
    nile4_set_pci_irq_polarity(4, 1);

    for (i = 0; i < 16; i++)
	nile4_clear_irq(i);

    /* Enable CPU int #0 */
    nile4_enable_irq_output(0);

    request_mem_region(NILE4_BASE, NILE4_SIZE, "Nile 4");
}


/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */

static struct irqaction irq2  = { no_action, 0, 0, "cascade", NULL, NULL };


void disable_irq(unsigned int irq_nr)
{
    if (is_i8259_irq(irq_nr))
	i8259_disable_irq(irq_nr);
    else
	nile4_disable_irq(irq_to_nile4(irq_nr));
}

void enable_irq(unsigned int irq_nr)
{
    if (is_i8259_irq(irq_nr))
	i8259_enable_irq(irq_nr);
    else
	nile4_enable_irq(irq_to_nile4(irq_nr));
}

int table[16] = { 0, };

void ddb_local0_irqdispatch(struct pt_regs *regs)
{
    u32 mask;
    int nile4_irq;
#if 1
    volatile static int nesting = 0;
    if (nesting++ == 0)
	ddb5074_led_d3(1);
    ddb5074_led_hex(nesting < 16 ? nesting : 15);
#endif

    mask = nile4_get_irq_stat(0);
    nile4_clear_irq_mask(mask);

    /* Handle the timer interrupt first */
    if (mask & (1<<NILE4_INT_GPT)) {
	nile4_disable_irq(NILE4_INT_GPT);
	do_IRQ(nile4_to_irq(NILE4_INT_GPT), regs);
	nile4_enable_irq(NILE4_INT_GPT);
	mask &= ~(1<<NILE4_INT_GPT);
    }
    for (nile4_irq = 0; mask; nile4_irq++, mask >>= 1)
	if (mask & 1) {
	    nile4_disable_irq(nile4_irq);
	    if (nile4_irq == NILE4_INT_INTE) {
		int i8259_irq = nile4_i8259_iack();
		i8259_do_irq(i8259_irq, regs);
	    } else
		do_IRQ(nile4_to_irq(nile4_irq), regs);
	    nile4_enable_irq(nile4_irq);
	}

#if 1
    if (--nesting == 0)
	ddb5074_led_d3(0);
    ddb5074_led_hex(nesting < 16 ? nesting : 15);
#endif
}

void ddb_local1_irqdispatch(void)
{
    printk("ddb_local1_irqdispatch called\n");
}

void ddb_buserror_irq(void)
{
    printk("ddb_buserror_irq called\n");
}

void ddb_8254timer_irq(void)
{
    printk("ddb_8254timer_irq called\n");
}

void __init ddb_irq_setup(void)
{
#ifdef CONFIG_REMOTE_DEBUG
    if (remote_debug)
	set_debug_traps();
    breakpoint(); /* you may move this line to whereever you want :-) */
#endif
    request_region(0x20, 0x20, "pic1");
    request_region(0xa0, 0x20, "pic2");
    i8259_setup_irq(2, &irq2);

    nile4_irq_setup();
    m1543_irq_setup();

    set_except_vector(0, ddbIRQ);
}

