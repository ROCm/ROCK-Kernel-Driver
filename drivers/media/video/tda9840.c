 /*
    tda9840.h - i2c-driver for the tda9840 by SGS Thomson   

    Copyright (C) 1998-2003 Michael Hunold <michael@mihu.de>

    The tda9840 is a stereo/dual sound processor with digital
    identification. It can be found at address 0x84 on the i2c-bus.

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

#include "tda9840.h"

static int debug = 0;	/* insmod parameter */
MODULE_PARM(debug,"i");
#define dprintk	if (debug) printk

#define	SWITCH		0x00
#define	LEVEL_ADJUST	0x02
#define	STEREO_ADJUST	0x03
#define	TEST		0x04

/* addresses to scan, found only at 0x42 (7-Bit) */
static unsigned short normal_i2c[] = {I2C_TDA9840, I2C_CLIENT_END};
static unsigned short normal_i2c_range[] = {I2C_CLIENT_END};

/* magic definition of all other variables and things */
I2C_CLIENT_INSMOD;

/* unique ID allocation */
static int tda9840_id = 0;

static struct i2c_driver driver;

static int tda9840_command(struct i2c_client *client, unsigned int cmd, void* arg)
{
	int result = 0;

	switch (cmd) {
		case TDA9840_SWITCH:
		{
			int byte = *(int*)arg;

			dprintk("tda9840.o: TDA9840_SWITCH: 0x%02x\n",byte);

			if (    byte != TDA9840_SET_MONO
			     && byte != TDA9840_SET_MUTE
			     && byte != TDA9840_SET_STEREO
			     && byte != TDA9840_SET_LANG1
			     && byte != TDA9840_SET_LANG2
			     && byte != TDA9840_SET_BOTH
			     && byte != TDA9840_SET_BOTH_R
			     && byte != TDA9840_SET_EXTERNAL ) {
				return -EINVAL;
			}
			
			if ( 0 != (result = i2c_smbus_write_byte_data(client, SWITCH, byte))) {
		 		printk("tda9840.o: TDA9840_SWITCH error.\n");
 				return -EFAULT;
			}
			
			return 0;
		}

		case TDA9840_LEVEL_ADJUST:
		{
			int  byte = *(int*)arg;

			dprintk("tda9840.o: TDA9840_LEVEL_ADJUST: %d\n",byte);

			/* check for correct range */
			if ( byte > 25 || byte < -20 )
				return -EINVAL;
			
			/* calculate actual value to set, see specs, page 18 */
			byte /= 5;
			if ( 0 < byte )
				byte += 0x8;
			else
				byte = -byte;

			if ( 0 != (result = i2c_smbus_write_byte_data(client, LEVEL_ADJUST, byte))) {
		 		printk("tda9840.o: TDA9840_LEVEL_ADJUST error.\n");
 				return -EFAULT;
			}
			
			return 0;
		}

		case TDA9840_STEREO_ADJUST:
		{
			int  byte = *(int*)arg;

			dprintk("tda9840.o: TDA9840_STEREO_ADJUST: %d\n",byte);

			/* check for correct range */
			if ( byte > 25 || byte < -24 )
				return -EINVAL;
			
			/* calculate actual value to set */
			byte /= 5;
			if ( 0 < byte )
				byte += 0x20;
			else
				byte = -byte;

			if ( 0 != (result = i2c_smbus_write_byte_data(client, STEREO_ADJUST, byte))) {
		 		printk("tda9840.o: TDA9840_STEREO_ADJUST error.\n");
 				return -EFAULT;
			}
			
			return 0;
		}

		case TDA9840_DETECT:
		{
			int byte = 0x0;

			if ( -1 == (byte = i2c_smbus_read_byte_data(client, STEREO_ADJUST))) {
		 		printk("tda9840.o: TDA9840_DETECT error while reading.\n");
				return -EFAULT;
			}			

			if( 0 != (byte & 0x80)) {
		 		dprintk("tda9840.o: TDA9840_DETECT, register contents invalid.\n");
				return -EFAULT;
			}

			dprintk("tda9840.o: TDA9840_DETECT, result: 0x%02x (original byte)\n",byte);

			return ((byte & 0x60) >> 5);				
		}

		case TDA9840_TEST:
		{
			int  byte = *(int*)arg;

			dprintk("tda9840.o: TDA9840_TEST: 0x%02x\n",byte);

			/* mask out irrelevant bits */
			byte &= 0x3;

			if ( 0 != (result = i2c_smbus_write_byte_data(client, TEST, byte))) {
		 		printk("tda9840.o: TDA9840_TEST error.\n");
 				return -EFAULT;
			}
		
			return 0;
		}

		default:
			return -ENOIOCTLCMD;
	}

	return 0;
}

static int tda9840_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct	i2c_client *client;
	int result = 0;

	int byte = 0x0;
			
	/* let's see whether this adapter can support what we need */
	if ( 0 == i2c_check_functionality(adapter, I2C_FUNC_SMBUS_READ_BYTE_DATA|I2C_FUNC_SMBUS_WRITE_BYTE_DATA)) {
		return 0;
	}

	/* allocate memory for client structure */
	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
        if (0 == client) {
		printk("tda9840.o: not enough kernel memory.\n");
		return -ENOMEM;
	}
	memset(client, 0, sizeof(struct i2c_client));
	
	/* fill client structure */
	sprintf(client->name,"tda9840 (0x%02x)", address);
	client->id = tda9840_id++;
	client->flags = 0;
	client->addr = address;
	client->adapter = adapter;
	client->driver = &driver;
	i2c_set_clientdata(client, NULL);

	/* tell the i2c layer a new client has arrived */
	if (0 != (result = i2c_attach_client(client))) {
		kfree(client);
		return result;
	}

	/* set initial values for level & stereo - adjustment, mode */
	byte = 0;
	if ( 0 != (result = tda9840_command(client, TDA9840_LEVEL_ADJUST, &byte))) {
 		printk("tda9840.o: could not initialize ic #1. continuing anyway. (result:%d)\n",result);
	}
	
	if ( 0 != (result = tda9840_command(client, TDA9840_STEREO_ADJUST, &byte))) {
 		printk("tda9840.o: could not initialize ic #2. continuing anyway. (result:%d)\n",result);
	}

	byte = TDA9840_SET_MONO;
	if ( 0 != (result = tda9840_command(client, TDA9840_SWITCH, &byte))) {
 		printk("tda9840.o: could not initialize ic #3. continuing anyway. (result:%d)\n",result);
	} 
	
	printk("tda9840.o: detected @ 0x%02x on adapter %s\n",2*address,&client->adapter->name[0]);

	return 0;
}

static int tda9840_attach(struct i2c_adapter *adapter)
{
	/* let's see whether this is a know adapter we can attach to */
	if( adapter->id != I2C_ALGO_SAA7146 ) {
		dprintk("tda9840.o: refusing to probe on unknown adapter [name='%s',id=0x%x]\n",adapter->name,adapter->id);
		return -ENODEV;
	}

	return i2c_probe(adapter,&addr_data,&tda9840_detect);
}

static int tda9840_detach(struct i2c_client *client)
{
	int err = 0;

	if ( 0 != (err = i2c_detach_client(client))) {
		printk("tda9840.o: Client deregistration failed, client not detached.\n");
		return err;
	}
	
	kfree(client);

	return 0;
}

static struct i2c_driver driver = {
	.owner		= THIS_MODULE,
	.name		= "tda9840 driver",
	.id		= I2C_DRIVERID_TDA9840,
	.flags		= I2C_DF_NOTIFY,
        .attach_adapter = tda9840_attach,
        .detach_client	= tda9840_detach,
        .command	= tda9840_command,
};

static int tda9840_init_module(void)
{
        i2c_add_driver(&driver);
        return 0;
}

static void tda9840_cleanup_module(void)
{
        i2c_del_driver(&driver);
}

module_init(tda9840_init_module);
module_exit(tda9840_cleanup_module);

MODULE_AUTHOR("Michael Hunold <michael@mihu.de>");
MODULE_DESCRIPTION("tda9840 driver");
MODULE_LICENSE("GPL");

