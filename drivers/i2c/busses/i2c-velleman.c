/* ------------------------------------------------------------------------- */
/* i2c-velleman.c i2c-hw access for Velleman K9000 adapters		     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-96, 2000 Simon G. Vogl

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

/* $Id: i2c-velleman.c,v 1.29 2003/01/21 08:08:16 kmalkki Exp $ */

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <asm/io.h>

/* ----- global defines -----------------------------------------------	*/
#define DEB(x)		/* should be reasonable open, close &c. 	*/
#define DEB2(x) 	/* low level debugging - very slow 		*/
#define DEBE(x)	x	/* error messages 				*/

					/* Pin Port  Inverted	name	*/
#define I2C_SDA		0x02		/*  ctrl bit 1 	(inv)	*/
#define I2C_SCL		0x08		/*  ctrl bit 3 	(inv)	*/

#define I2C_SDAIN	0x10		/* stat bit 4		*/
#define I2C_SCLIN	0x08		/* ctrl bit 3 (inv)(reads own output)*/

#define I2C_DMASK	0xfd
#define I2C_CMASK	0xf7


/* --- Convenience defines for the parallel port:			*/
#define BASE	(unsigned int)(data)
#define DATA	BASE			/* Centronics data port		*/
#define STAT	(BASE+1)		/* Centronics status port	*/
#define CTRL	(BASE+2)		/* Centronics control port	*/

#define DEFAULT_BASE 0x378
static int base=0;

/* ----- local functions --------------------------------------------------- */

static void bit_velle_setscl(void *data, int state)
{
	if (state) {
		outb(inb(CTRL) & I2C_CMASK,   CTRL);
	} else {
		outb(inb(CTRL) | I2C_SCL, CTRL);
	}
	
}

static void bit_velle_setsda(void *data, int state)
{
	if (state) {
		outb(inb(CTRL) & I2C_DMASK , CTRL);
	} else {
		outb(inb(CTRL) | I2C_SDA, CTRL);
	}
	
} 

static int bit_velle_getscl(void *data)
{
	return ( 0 == ( (inb(CTRL)) & I2C_SCLIN ) );
}

static int bit_velle_getsda(void *data)
{
	return ( 0 != ( (inb(STAT)) & I2C_SDAIN ) );
}

static int bit_velle_init(void)
{
	if (!request_region(base, (base == 0x3bc) ? 3 : 8, 
			"i2c (Vellemann adapter)"))
		return -ENODEV;

	bit_velle_setsda((void*)base,1);
	bit_velle_setscl((void*)base,1);
	return 0;
}

/* ------------------------------------------------------------------------
 * Encapsulate the above functions in the correct operations structure.
 * This is only done when more than one hardware adapter is supported.
 */

static struct i2c_algo_bit_data bit_velle_data = {
	.setsda		= bit_velle_setsda,
	.setscl		= bit_velle_setscl,
	.getsda		= bit_velle_getsda,
	.getscl		= bit_velle_getscl,
	.udelay		= 10,
	.mdelay		= 10,
	.timeout	= HZ
};

static struct i2c_adapter bit_velle_ops = {
	.owner		= THIS_MODULE,
	.algo_data	= &bit_velle_data,
	.name		= "Velleman K8000",
};

static int __init i2c_bitvelle_init(void)
{
	printk(KERN_INFO "i2c-velleman: i2c Velleman K8000 driver\n");
	if (base==0) {
		/* probe some values */
		base=DEFAULT_BASE;
		bit_velle_data.data=(void*)DEFAULT_BASE;
		if (bit_velle_init()==0) {
			if(i2c_bit_add_bus(&bit_velle_ops) < 0)
				return -ENODEV;
		} else {
			return -ENODEV;
		}
	} else {
		bit_velle_data.data=(void*)base;
		if (bit_velle_init()==0) {
			if(i2c_bit_add_bus(&bit_velle_ops) < 0)
				return -ENODEV;
		} else {
			return -ENODEV;
		}
	}
	printk(KERN_DEBUG "i2c-velleman: found device at %#x.\n",base);
	return 0;
}

static void __exit i2c_bitvelle_exit(void)
{	
	i2c_bit_del_bus(&bit_velle_ops);
	release_region(base, (base == 0x3bc) ? 3 : 8);
}

MODULE_AUTHOR("Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C-Bus adapter routines for Velleman K8000 adapter");
MODULE_LICENSE("GPL");

MODULE_PARM(base, "i");

module_init(i2c_bitvelle_init);
module_exit(i2c_bitvelle_exit);
