 /*
    tea6420.o - i2c-driver for the tea6420 by SGS Thomson

    Copyright (C) 1998-2003 Michael Hunold <michael@mihu.de>

    The tea6420 is a bus controlled audio-matrix with 5 stereo inputs,
    4 stereo outputs and gain control for each output.
    It is cascadable, i.e. it can be found at the adresses 0x98
    and 0x9a on the i2c-bus.
    
    For detailed informations download the specifications directly
    from SGS Thomson at http://www.st.com
    
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

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/init.h>

#include "tea6420.h"

static int debug = 0;	/* insmod parameter */
MODULE_PARM(debug,"i");
#define dprintk	if (debug) printk

/* addresses to scan, found only at 0x4c and/or 0x4d (7-Bit) */
static unsigned short normal_i2c[] = {I2C_TEA6420_1, I2C_TEA6420_2, I2C_CLIENT_END};
static unsigned short normal_i2c_range[] = {I2C_CLIENT_END};

/* magic definition of all other variables and things */
I2C_CLIENT_INSMOD;

static struct i2c_driver driver;

/* unique ID allocation */
static int tea6420_id = 0;

/* make a connection between the input 'i' and the output 'o'
   with gain 'g' for the tea6420-client 'client' (note: i = 6 means 'mute') */
static int tea6420_switch(struct i2c_client *client, int i, int o, int g)
{
	u8 	byte = 0;
	
	int 	result = 0;
	
	dprintk("tea6420.o: tea6420_switch: adr:0x%02x, i:%d, o:%d, g:%d\n",client->addr,i,o,g);

	/* check if the paramters are valid */
	if ( i < 1 || i > 6 || o < 1 || o > 4 || g < 0 || g > 6 || g%2 != 0 )
		return -1;

	byte  = ((o-1)<<5);
	byte |=  (i-1);

	/* to understand this, have a look at the tea6420-specs (p.5) */
	switch(g) {
		case 0:
			byte |= (3<<3);
			break;
		case 2:
			byte |= (2<<3);
			break;
		case 4:
			byte |= (1<<3);
			break;
		case 6:
			break;
	}

	/* fixme?: 1 != ... => 0 != */
	if ( 0 != (result = i2c_smbus_write_byte(client,byte))) {
		printk("tea6402:%d\n",result);
		dprintk(KERN_ERR "tea6420.o: could not switch, result:%d\n",result);
		return -EFAULT;
	}
	
	return 0;
}

/* this function is called by i2c_probe */
static int tea6420_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct	i2c_client *client;
	int err = 0, i = 0;

	/* let's see whether this adapter can support what we need */
	if ( 0 == i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WRITE_BYTE)) {
		return 0;
	}

	/* allocate memory for client structure */
	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
        if (0 == client) {
		return -ENOMEM;
	}
	memset(client, 0x0, sizeof(struct i2c_client));	

	/* fill client structure */
	sprintf(client->name,"tea6420 (0x%02x)", address);
	client->id = tea6420_id++;
	client->flags = 0;
	client->addr = address;
	client->adapter = adapter;
	client->driver = &driver;
	i2c_set_clientdata(client, NULL);

	/* tell the i2c layer a new client has arrived */
	if (0 != (err = i2c_attach_client(client))) {
		kfree(client);
		return err;
	}

	/* set initial values: set "mute"-input to all outputs at gain 0 */
	err = 0;
	for(i = 1; i < 5; i++) {
		err += tea6420_switch(client, 6, i, 0);
	}
	if( 0 != err) {
		printk("tea6420.o: could not initialize chipset. continuing anyway.\n");
	}
	
	printk("tea6420.o: detected @ 0x%02x on adapter %s\n",2*address,&client->adapter->name[0]);

	return 0;
}

static int tea6420_attach(struct i2c_adapter *adapter)
{
	/* let's see whether this is a know adapter we can attach to */
	if( adapter->id != I2C_ALGO_SAA7146 ) {
		dprintk("tea6420.o: refusing to probe on unknown adapter [name='%s',id=0x%x]\n",adapter->name,adapter->id);
		return -ENODEV;
	}

	return i2c_probe(adapter,&addr_data,&tea6420_detect);
}

static int tea6420_detach(struct i2c_client *client)
{
	int err = 0;

	if ( 0 != (err = i2c_detach_client(client))) {
		printk("tea6420.o: Client deregistration failed, client not detached.\n");
		return err;
	}
	
	kfree(client);

	return 0;
}

static int tea6420_command(struct i2c_client *client, unsigned int cmd, void* arg)
{
	struct tea6420_multiplex *a = (struct tea6420_multiplex*)arg;
	int result = 0;

	switch (cmd) {
		case TEA6420_SWITCH: {
			result = tea6420_switch(client,a->in,a->out,a->gain);
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
	.name		= "tea6420 driver",
	.id		= I2C_DRIVERID_TEA6420,
	.flags		= I2C_DF_NOTIFY,
        .attach_adapter = tea6420_attach,
        .detach_client	= tea6420_detach,
        .command	= tea6420_command,
};

static int __init tea6420_init_module(void)
{
	return i2c_add_driver(&driver);
}

static void __exit tea6420_cleanup_module(void)
{
        i2c_del_driver(&driver);
}

module_init(tea6420_init_module);
module_exit(tea6420_cleanup_module);

MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_DESCRIPTION("tea6420 driver");
MODULE_LICENSE("GPL");
