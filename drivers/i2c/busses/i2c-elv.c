/* ------------------------------------------------------------------------- */
/* i2c-elv.c i2c-hw access for philips style parallel port adapters	     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-2000 Simon G. Vogl

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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <asm/io.h>

#define DEFAULT_BASE 0x378
static int base=0;
static unsigned char port_data = 0;

/* ----- global defines -----------------------------------------------	*/
#define DEB(x)		/* should be reasonable open, close &c. 	*/
#define DEB2(x) 	/* low level debugging - very slow 		*/
#define DEBE(x)	x	/* error messages 				*/
#define DEBINIT(x) x	/* detection status messages			*/

/* --- Convenience defines for the parallel port:			*/
#define BASE	(unsigned int)(data)
#define DATA	BASE			/* Centronics data port		*/
#define STAT	(BASE+1)		/* Centronics status port	*/
#define CTRL	(BASE+2)		/* Centronics control port	*/


/* ----- local functions ----------------------------------------------	*/


static void bit_elv_setscl(void *data, int state)
{
	if (state) {
		port_data &= 0xfe;
	} else {
		port_data |=1;
	}
	outb(port_data, DATA);
}

static void bit_elv_setsda(void *data, int state)
{
	if (state) {
		port_data &=0xfd;
	} else {
		port_data |=2;
	}
	outb(port_data, DATA);
} 

static int bit_elv_getscl(void *data)
{
	return ( 0 == ( (inb_p(STAT)) & 0x08 ) );
}

static int bit_elv_getsda(void *data)
{
	return ( 0 == ( (inb_p(STAT)) & 0x40 ) );
}

static int bit_elv_init(void)
{
	if (!request_region(base, (base == 0x3bc) ? 3 : 8,
				"i2c (ELV adapter)"))
		return -ENODEV;

	if (inb(base+1) & 0x80) {	/* BUSY should be high	*/
		DEBINIT(printk(KERN_DEBUG "i2c-elv.o: Busy was low.\n"));
		goto fail;
	} 

	outb(0x0c,base+2);	/* SLCT auf low		*/
	udelay(400);
	if (!(inb(base+1) && 0x10)) {
		outb(0x04,base+2);
		DEBINIT(printk(KERN_DEBUG "i2c-elv.o: Select was high.\n"));
		goto fail;
	}

	port_data = 0;
	bit_elv_setsda((void*)base,1);
	bit_elv_setscl((void*)base,1);
	return 0;

fail:
	release_region(base , (base == 0x3bc) ? 3 : 8);
	return -ENODEV;
}

/* ------------------------------------------------------------------------
 * Encapsulate the above functions in the correct operations structure.
 * This is only done when more than one hardware adapter is supported.
 */
static struct i2c_algo_bit_data bit_elv_data = {
	.setsda		= bit_elv_setsda,
	.setscl		= bit_elv_setscl,
	.getsda		= bit_elv_getsda,
	.getscl		= bit_elv_getscl,
	.udelay		= 80,
	.mdelay		= 80,
	.timeout	= HZ
};

static struct i2c_adapter bit_elv_ops = {
	.owner		= THIS_MODULE,
	.algo_data	= &bit_elv_data,
	.name		= "ELV Parallel port adaptor",
};

static int __init i2c_bitelv_init(void)
{
	printk(KERN_INFO "i2c ELV parallel port adapter driver\n");
	if (base==0) {
		/* probe some values */
		base=DEFAULT_BASE;
		bit_elv_data.data=(void*)DEFAULT_BASE;
		if (bit_elv_init()==0) {
			if(i2c_bit_add_bus(&bit_elv_ops) < 0)
				return -ENODEV;
		} else {
			return -ENODEV;
		}
	} else {
		i2c_set_adapdata(&bit_elv_ops, (void *)base);
		if (bit_elv_init()==0) {
			if(i2c_bit_add_bus(&bit_elv_ops) < 0)
				return -ENODEV;
		} else {
			return -ENODEV;
		}
	}
	printk(KERN_DEBUG "i2c-elv.o: found device at %#x.\n",base);
	return 0;
}

static void __exit i2c_bitelv_exit(void)
{
	i2c_bit_del_bus(&bit_elv_ops);
	release_region(base , (base == 0x3bc) ? 3 : 8);
}

MODULE_AUTHOR("Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C-Bus adapter routines for ELV parallel port adapter");
MODULE_LICENSE("GPL");

MODULE_PARM(base, "i");

module_init(i2c_bitelv_init);
module_exit(i2c_bitelv_exit);
