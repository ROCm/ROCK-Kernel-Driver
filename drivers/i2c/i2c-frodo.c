
/*
 * linux/drivers/i2c/i2c-frodo.c
 *
 * Author: Abraham van der Merwe <abraham@2d3d.co.za>
 *
 * An I2C adapter driver for the 2d3D, Inc. StrongARM SA-1110
 * Development board (Frodo).
 *
 * This source code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/hardware.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

static void frodo_setsda (void *data,int state)
{
	if (state)
		FRODO_CPLD_I2C |= FRODO_I2C_SDA_OUT;
	else
		FRODO_CPLD_I2C &= ~FRODO_I2C_SDA_OUT;
}

static void frodo_setscl (void *data,int state)
{
	if (state)
		FRODO_CPLD_I2C |= FRODO_I2C_SCL_OUT;
	else
		FRODO_CPLD_I2C &= ~FRODO_I2C_SCL_OUT;
}

static int frodo_getsda (void *data)
{
	return ((FRODO_CPLD_I2C & FRODO_I2C_SDA_IN) != 0);
}

static int frodo_getscl (void *data)
{
	return ((FRODO_CPLD_I2C & FRODO_I2C_SCL_IN) != 0);
}

static struct i2c_algo_bit_data bit_frodo_data = {
	setsda:		frodo_setsda,
	setscl:		frodo_setscl,
	getsda:		frodo_getsda,
	getscl:		frodo_getscl,
	udelay:		80,
	mdelay:		80,
	timeout:	100
};

static int frodo_client_register (struct i2c_client *client)
{
	return (0);
}

static int frodo_client_unregister (struct i2c_client *client)
{
	return (0);
}

static void frodo_inc_use (struct i2c_adapter *adapter)
{
	MOD_INC_USE_COUNT;
}

static void frodo_dec_use (struct i2c_adapter *adapter)
{
	MOD_DEC_USE_COUNT;
}

static struct i2c_adapter frodo_ops = {
	name:				"Frodo adapter driver",
	id:					I2C_HW_B_FRODO,
	algo:				NULL,
	algo_data:			&bit_frodo_data,
	inc_use:			frodo_inc_use,
	dec_use:			frodo_dec_use,
	client_register:	frodo_client_register,
	client_unregister:	frodo_client_unregister
};

static int __init i2c_frodo_init (void)
{
	return (i2c_bit_add_bus (&frodo_ops));
}

EXPORT_NO_SYMBOLS;

static void __exit i2c_frodo_exit (void)
{
	i2c_bit_del_bus (&frodo_ops);
}

MODULE_AUTHOR ("Abraham van der Merwe <abraham@2d3d.co.za>");
MODULE_DESCRIPTION ("I2C-Bus adapter routines for Frodo");

#ifdef MODULE_LICENSE
MODULE_LICENSE ("GPL");
#endif	/* #ifdef MODULE_LICENSE */

EXPORT_NO_SYMBOLS;

module_init (i2c_frodo_init);
module_exit (i2c_frodo_exit);

