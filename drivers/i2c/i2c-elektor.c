/* ------------------------------------------------------------------------- */
/* i2c-elektor.c i2c-hw access for PCF8584 style isa bus adaptes             */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-97 Simon G. Vogl
                   1998-99 Hans Berglund

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */

/* With some changes from Kyösti Mälkki <kmalkki@cc.hut.fi> and even
   Frodo Looijaard <frodol@dds.nl> */

/* $Id: i2c-elektor.c,v 1.19 2000/07/25 23:52:17 frodo Exp $ */

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/malloc.h>
#include <linux/version.h>
#include <linux/init.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-pcf.h>
#include <linux/i2c-elektor.h>
#include "i2c-pcf8584.h"

#define DEFAULT_BASE 0x300
#define DEFAULT_IRQ      0
#define DEFAULT_CLOCK 0x1c
#define DEFAULT_OWN   0x55

static int base  = 0;
static int irq   = 0;
static int clock = 0;
static int own   = 0;
static int i2c_debug=0;
static struct i2c_pcf_isa gpi;
#if (LINUX_VERSION_CODE < 0x020301)
static struct wait_queue *pcf_wait = NULL;
#else
static wait_queue_head_t pcf_wait;
#endif
static int pcf_pending;

/* ----- global defines -----------------------------------------------	*/
#define DEB(x)	if (i2c_debug>=1) x
#define DEB2(x) if (i2c_debug>=2) x
#define DEB3(x) if (i2c_debug>=3) x
#define DEBE(x)	x	/* error messages 				*/


/* --- Convenience defines for the i2c port:			*/
#define BASE	((struct i2c_pcf_isa *)(data))->pi_base
#define DATA	BASE			/* Adapter data port		*/
#define CTRL	(BASE+1)		/* Adapter control port	        */

/* ----- local functions ----------------------------------------------	*/

static void pcf_isa_setbyte(void *data, int ctl, int val)
{
        unsigned long j = jiffies + 10;

        if (ctl) {
		if (gpi.pi_irq > 0) {
			DEB3(printk("i2c-elektor.o: Write Ctrl 0x%02X\n",
			     val|I2C_PCF_ENI));
                        DEB3({while (jiffies < j) schedule();})
			outb(val | I2C_PCF_ENI, CTRL);
		} else {
			 DEB3(printk("i2c-elektor.o: Write Ctrl 0x%02X\n", val|I2C_PCF_ENI));
                         DEB3({while (jiffies < j) schedule();})
			 outb(val|I2C_PCF_ENI, CTRL);
		}
	} else {
		DEB3(printk("i2c-elektor.o: Write Data 0x%02X\n", val&0xff));
                DEB3({while (jiffies < j) schedule();})
		outb(val, DATA);
	}
}

static int pcf_isa_getbyte(void *data, int ctl)
{
	int val;

	if (ctl) {
		val = inb(CTRL);
		DEB3(printk("i2c-elektor.o: Read Ctrl 0x%02X\n", val));
	} else {
		val = inb(DATA);
		DEB3(printk("i2c-elektor.o: Read Data 0x%02X\n", val));
	}
	return (val);
}

static int pcf_isa_getown(void *data)
{
	return (gpi.pi_own);
}


static int pcf_isa_getclock(void *data)
{
	return (gpi.pi_clock);
}



#if 0
static void pcf_isa_sleep(unsigned long timeout)
{
	schedule_timeout( timeout * HZ);
}
#endif


static void pcf_isa_waitforpin(void) {

	int timeout = 2;

	if (gpi.pi_irq > 0) {
		cli();
	if (pcf_pending == 0) {
		interruptible_sleep_on_timeout(&pcf_wait, timeout*HZ );
	} else
		pcf_pending = 0;
		sti();
	} else {
		udelay(100);
	}
}


static void pcf_isa_handler(int this_irq, void *dev_id, struct pt_regs *regs) {
	pcf_pending = 1;
	wake_up_interruptible(&pcf_wait);
}


static int pcf_isa_init(void)
{
	if (check_region(gpi.pi_base, 2) < 0 ) {
		return -ENODEV;
	} else {
		request_region(gpi.pi_base, 2, "i2c (isa bus adapter)");
	}
	if (gpi.pi_irq > 0) {
		if (request_irq(gpi.pi_irq, pcf_isa_handler, 0, "PCF8584", 0)
		    < 0) {
		printk("i2c-elektor.o: Request irq%d failed\n", gpi.pi_irq);
		gpi.pi_irq = 0;
	} else
		enable_irq(gpi.pi_irq);
	}
	return 0;
}


static void pcf_isa_exit(void)
{
	if (gpi.pi_irq > 0) {
		disable_irq(gpi.pi_irq);
		free_irq(gpi.pi_irq, 0);
	}
	release_region(gpi.pi_base , 2);
}


static int pcf_isa_reg(struct i2c_client *client)
{
	return 0;
}


static int pcf_isa_unreg(struct i2c_client *client)
{
	return 0;
}

static void pcf_isa_inc_use(struct i2c_adapter *adap)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

static void pcf_isa_dec_use(struct i2c_adapter *adap)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}


/* ------------------------------------------------------------------------
 * Encapsulate the above functions in the correct operations structure.
 * This is only done when more than one hardware adapter is supported.
 */
static struct i2c_algo_pcf_data pcf_isa_data = {
	NULL,
	pcf_isa_setbyte,
	pcf_isa_getbyte,
	pcf_isa_getown,
	pcf_isa_getclock,
	pcf_isa_waitforpin,
	80, 80, 100,		/*	waits, timeout */
};

static struct i2c_adapter pcf_isa_ops = {
	"PCF8584 ISA adapter",
	I2C_HW_P_ELEK,
	NULL,
	&pcf_isa_data,
	pcf_isa_inc_use,
	pcf_isa_dec_use,
	pcf_isa_reg,
	pcf_isa_unreg,
};

int __init i2c_pcfisa_init(void) 
{

	struct i2c_pcf_isa *pisa = &gpi;

	printk("i2c-elektor.o: i2c pcf8584-isa adapter module\n");
	if (base == 0)
		pisa->pi_base = DEFAULT_BASE;
	else
		pisa->pi_base = base;

	if (irq == 0)
		pisa->pi_irq = DEFAULT_IRQ;
	else
		pisa->pi_irq = irq;

	if (clock == 0)
		pisa->pi_clock = DEFAULT_CLOCK;
	else
		pisa->pi_clock = clock;

	if (own == 0)
		pisa->pi_own = DEFAULT_OWN;
	else
		pisa->pi_own = own;

	pcf_isa_data.data = (void *)pisa;
#if (LINUX_VERSION_CODE >= 0x020301)
	init_waitqueue_head(&pcf_wait);
#endif
	if (pcf_isa_init() == 0) {
		if (i2c_pcf_add_bus(&pcf_isa_ops) < 0)
			return -ENODEV;
	} else {
		return -ENODEV;
	}
	printk("i2c-elektor.o: found device at %#x.\n", pisa->pi_base);
	return 0;
}


EXPORT_NO_SYMBOLS;

#ifdef MODULE
MODULE_AUTHOR("Hans Berglund <hb@spacetec.no>");
MODULE_DESCRIPTION("I2C-Bus adapter routines for PCF8584 ISA bus adapter");

MODULE_PARM(base, "i");
MODULE_PARM(irq, "i");
MODULE_PARM(clock, "i");
MODULE_PARM(own, "i");
MODULE_PARM(i2c_debug,"i");

int init_module(void) 
{
	return i2c_pcfisa_init();
}

void cleanup_module(void) 
{
	i2c_pcf_del_bus(&pcf_isa_ops);
	pcf_isa_exit();
}

#endif
