 /*
    tea6415c.h - i2c-driver for the tea6415c by SGS Thomson   

    Copyright (C) 1998-2003 Michael Hunold <michael@mihu.de>

    The tea6415c is a bus controlled video-matrix-switch
    with 8 inputs and 6 outputs.
    It is cascadable, i.e. it can be found at the addresses
    0x86 and 0x06 on the i2c-bus.
    
    For detailed informations download the specifications directly
    from SGS Thomson at http://www.st.com
        
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License vs published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mvss Ave, Cambridge, MA 02139, USA.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include "tea6415c.h"

static int debug = 0;	/* insmod parameter */
MODULE_PARM(debug,"i");
#define dprintk	if (debug) printk

#define TEA6415C_NUM_INPUTS	8
#define TEA6415C_NUM_OUTPUTS	6

/* addresses to scan, found only at 0x03 and/or 0x43 (7-bit) */
static unsigned short normal_i2c[] = {I2C_TEA6415C_1, I2C_TEA6415C_2, I2C_CLIENT_END};
static unsigned short normal_i2c_range[] = {I2C_CLIENT_END};

/* magic definition of all other variables and things */
I2C_CLIENT_INSMOD;

static struct i2c_driver driver;

/* unique ID allocation */
static int tea6415c_id = 0;

/* this function is called by i2c_probe */
static int tea6415c_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct	i2c_client *client = 0;
	int err = 0;

	/* let's see whether this adapter can support what we need */
	if ( 0 == i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WRITE_BYTE)) {
		return 0;
	}

	/* allocate memory for client structure */
	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
        if (0 == client) {
		return -ENOMEM;
	}
	memset(client, 0, sizeof(struct i2c_client));

	/* fill client structure */
	sprintf(client->name,"tea6415c (0x%02x)", address);
	client->id = tea6415c_id++;
	client->flags = 0;
	client->addr = address;
	client->adapter = adapter;
	client->driver = &driver;

	/* tell the i2c layer a new client has arrived */
	if (0 != (err = i2c_attach_client(client))) {
		kfree(client);
		return err;
	}

	printk("tea6415c.o: detected @ 0x%02x on adapter %s\n",2*address,&client->adapter->name[0]);

	return 0;
}

static int tea6415c_attach(struct i2c_adapter *adapter)
{
	/* let's see whether this is a know adapter we can attach to */
	if( adapter->id != I2C_ALGO_SAA7146 ) {
		dprintk("tea6415c.o: refusing to probe on unknown adapter [name='%s',id=0x%x]\n",adapter->name,adapter->id);
		return -ENODEV;
	}

	return i2c_probe(adapter,&addr_data,&tea6415c_detect);
}

static int tea6415c_detach(struct i2c_client *client)
{
	int err = 0;

	if ( 0 != (err = i2c_detach_client(client))) {
		printk("tea6415c.o: Client deregistration failed, client not detached.\n");
		return err;
	}
	
	kfree(client);

	return 0;
}

/* makes a connection between the input-pin 'i' and the output-pin 'o'
   for the tea6415c-client 'client' */
static int tea6415c_switch(struct i2c_client *client, int i, int o)
{
	u8 	byte = 0;
	
	dprintk("tea6415c.o: tea6415c_switch: adr:0x%02x, i:%d, o:%d\n", client->addr, i, o);
		
	/* check if the pins are valid */
	if ( 0 == ((  1 == i ||  3 == i ||  5 == i ||  6 == i ||  8 == i || 10 == i || 20 == i || 11 == i ) &&
		    (18 == o || 17 == o || 16 == o || 15 == o || 14 == o || 13 == o )))
		return -1;

	/* to understand this, have a look at the tea6415c-specs (p.5) */
	switch(o) {
		case 18:
			byte = 0x00;
			break;
		case 14:
			byte = 0x20;
			break;
		case 16:
			byte = 0x10;
			break;
		case 17:
			byte = 0x08;
			break;
		case 15:
			byte = 0x18;
			break;
		case 13:
			byte = 0x28;
			break;
	};
		
	switch(i) {
		case 5:
			byte |= 0x00;
			break;
		case 8:
			byte |= 0x04;
			break;
		case 3:
			byte |= 0x02;
			break;
		case 20:
			byte |= 0x06;
			break;
		case 6:
			byte |= 0x01;
			break;
		case 10:
			byte |= 0x05;
			break;
		case 1:
			byte |= 0x03;
			break;
		case 11:
			byte |= 0x07;
			break;
	};

	if ( 0 != i2c_smbus_write_byte(client,byte)) {
		dprintk("tea6415c.o: tea6415c_switch: could not write to tea6415c\n");
		return -1;
	}

	return 0;
}

static int tea6415c_command(struct i2c_client *client, unsigned int cmd, void* arg)
{
	struct tea6415c_multiplex *v = (struct tea6415c_multiplex*)arg;
	int result = 0;

	switch (cmd) {
		case TEA6415C_SWITCH: {
			result = tea6415c_switch(client,v->in,v->out);
			break;
		}
		default: {
			return -ENOIOCTLCMD;
		}
	}

	if ( 0 != result )
		return result;
	
	return 0;
}

static struct i2c_driver driver = {
	.owner		= THIS_MODULE,
	.name		= "tea6415c driver",
	.id		= I2C_DRIVERID_TEA6415C,
	.flags		= I2C_DF_NOTIFY,
        .attach_adapter = tea6415c_attach,
        .detach_client	= tea6415c_detach,
        .command	= tea6415c_command,
};

static int tea6415c_init_module(void)
{
	i2c_add_driver(&driver);
	return 0;
}

static void tea6415c_cleanup_module(void)
{
        i2c_del_driver(&driver);
}

module_init(tea6415c_init_module);
module_exit(tea6415c_cleanup_module);

MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_DESCRIPTION("tea6415c driver");
MODULE_LICENSE("GPL");

