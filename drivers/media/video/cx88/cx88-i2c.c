/*
    cx88-i2c.c  --  all the i2c code is here

    Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
                           & Marcus Metzler (mocm@thp.uni-koeln.de)
    (c) 2002 Yurij Sysoev <yurij@naturesoft.net>
    (c) 1999-2003 Gerd Knorr <kraxel@bytesex.org>

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
    
*/

#define __NO_VERSION__ 1

#include <linux/module.h>
#include <linux/init.h>

#include <asm/io.h>

#include "cx88.h"

/* ----------------------------------------------------------------------- */

void cx8800_bit_setscl(void *data, int state)
{
	struct cx8800_dev *dev = data;

	if (state)
		dev->i2c_state |= 0x02;
	else
		dev->i2c_state &= ~0x02;
	cx_write(MO_I2C, dev->i2c_state);
	cx_read(MO_I2C);
}

void cx8800_bit_setsda(void *data, int state)
{
	struct cx8800_dev *dev = data;

	if (state)
		dev->i2c_state |= 0x01;
	else
		dev->i2c_state &= ~0x01;
	cx_write(MO_I2C, dev->i2c_state);
	cx_read(MO_I2C);
}

static int cx8800_bit_getscl(void *data)
{
	struct cx8800_dev *dev = data;
	u32 state;
	
	state = cx_read(MO_I2C);
	return state & 0x02 ? 1 : 0;
}

static int cx8800_bit_getsda(void *data)
{
	struct cx8800_dev *dev = data;
	u32 state;

	state = cx_read(MO_I2C);
	return state & 0x01;
}

/* ----------------------------------------------------------------------- */

#ifndef I2C_PEC
static void cx8800_inc_use(struct i2c_adapter *adap)
{
	MOD_INC_USE_COUNT;
}

static void cx8800_dec_use(struct i2c_adapter *adap)
{
	MOD_DEC_USE_COUNT;
}
#endif

static int attach_inform(struct i2c_client *client)
{
        struct cx8800_dev *dev = i2c_get_adapdata(client->adapter);

	if (dev->tuner_type != UNSET)
		cx8800_call_i2c_clients(dev,TUNER_SET_TYPE,&dev->tuner_type);

        if (1 /* fixme: debug */)
		printk("%s: i2c attach [client=%s]\n",
		       dev->name, i2c_clientname(client));
        return 0;
}

void cx8800_call_i2c_clients(struct cx8800_dev *dev, unsigned int cmd, void *arg)
{
	if (0 != dev->i2c_rc)
		return;
	i2c_clients_command(&dev->i2c_adap, cmd, arg);
}

static struct i2c_algo_bit_data cx8800_i2c_algo_template = {
	.setsda  = cx8800_bit_setsda,
	.setscl  = cx8800_bit_setscl,
	.getsda  = cx8800_bit_getsda,
	.getscl  = cx8800_bit_getscl,
	.udelay  = 16,
	.mdelay  = 10,
	.timeout = 200,
};

/* ----------------------------------------------------------------------- */

static struct i2c_adapter cx8800_i2c_adap_template = {
#ifdef I2C_PEC
	.owner             = THIS_MODULE,
#else
	.inc_use           = cx8800_inc_use,
	.dec_use           = cx8800_dec_use,
#endif
#ifdef I2C_ADAP_CLASS_TV_ANALOG
	.class             = I2C_ADAP_CLASS_TV_ANALOG,
#endif
	I2C_DEVNAME("cx2388x"),
	.id                = I2C_HW_B_BT848,
	.client_register   = attach_inform,
};

static struct i2c_client cx8800_i2c_client_template = {
        I2C_DEVNAME("cx88xx internal"),
        .id   = -1,
};

/* init + register i2c algo-bit adapter */
int __devinit cx8800_i2c_init(struct cx8800_dev *dev)
{
	memcpy(&dev->i2c_adap, &cx8800_i2c_adap_template,
	       sizeof(dev->i2c_adap));
	memcpy(&dev->i2c_algo, &cx8800_i2c_algo_template,
	       sizeof(dev->i2c_algo));
	memcpy(&dev->i2c_client, &cx8800_i2c_client_template,
	       sizeof(dev->i2c_client));

	dev->i2c_adap.dev.parent = &dev->pci->dev;
	strlcpy(dev->i2c_adap.name,dev->name,sizeof(dev->i2c_adap.name));
        dev->i2c_algo.data = dev;
        i2c_set_adapdata(&dev->i2c_adap,dev);
        dev->i2c_adap.algo_data = &dev->i2c_algo;
        dev->i2c_client.adapter = &dev->i2c_adap;

	cx8800_bit_setscl(dev,1);
	cx8800_bit_setsda(dev,1);

	dev->i2c_rc = i2c_bit_add_bus(&dev->i2c_adap);
	printk("%s: i2c register %s\n", dev->name,
	       (0 == dev->i2c_rc) ? "ok" : "FAILED");
	return dev->i2c_rc;
}

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
