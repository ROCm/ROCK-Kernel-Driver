/*      $Id: lirc_i2c.c,v 1.20 2003/08/03 09:40:10 lirc Exp $      */

/*
 * i2c IR lirc plugin for Hauppauge and Pixelview cards - new 2.8.x i2c stack
 *
 * Copyright (c) 2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 * modified for PixelView (BT878P+W/FM) by
 *      Michal Kochanowicz <mkochano@pld.org.pl>
 *      Christoph Bartelmus <lirc@bartelmus.de>
 * modified for KNC ONE TV Station/Anubis Typhoon TView Tuner by
 *      Ulrich Mueller <ulrich.mueller42@web.de>
 * modified for Asus TV-Box and Creative/VisionTek BreakOut-Box by
 *      Stefan Jahn <stefan@lkcc.org>
 * modified for Linux 2.6 by
 * 	Jeffrey Clark <jeff@clarkmania.com>
 *
 * parts are cut&pasted from the old lirc_haup.c driver
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/moduleparam.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <asm/semaphore.h>

#include "../../media/video/bttv.h"

#include "lirc_dev.h"

static unsigned short normal_i2c[] = { 0x1a, 0x18, 0x4b, 0x64, 0x30, 0x21, 0x23, I2C_CLIENT_END };
static unsigned short normal_i2c_range[] = { I2C_CLIENT_END };
static unsigned short probe[2] = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short probe_range[2] = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short ignore[2] = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short ignore_range[2] = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short force[2] = { I2C_CLIENT_END, I2C_CLIENT_END };

static struct i2c_client_address_data addr_data = {
	.normal_i2c             = normal_i2c,
	.normal_i2c_range       = normal_i2c_range,
	.probe                  = probe,
	.probe_range            = probe_range,
	.ignore                 = ignore,
	.ignore_range           = ignore_range,
	.force                  = force
};

struct i2c_ir {
	struct lirc_plugin lirc;
	struct i2c_client client;
	int nextkey;
	unsigned char b[3];
	unsigned char bits;
	unsigned char flag;
};

/* ----------------------------------------------------------------------- */
/* insmod parameters                                                       */

static int debug   = 0;    /* debug output */
static int minor   = -1;   /* minor number */

MODULE_PARM(debug,"i");
MODULE_PARM(minor,"i");

MODULE_DESCRIPTION("Infrared receiver driver for Hauppauge and Pixelview cards (i2c stack)");
MODULE_AUTHOR("Gerd Knorr, Michal Kochanowicz, Christoph Bartelmus, Ulrich Mueller, Stefan Jahn, Jeffrey Clark");
MODULE_LICENSE("GPL");

#define dprintk if (debug) printk

/* ----------------------------------------------------------------------- */

#define DRIVER_NAME "lirc_i2c"

/* ----------------------------------------------------------------------- */

static inline int reverse(int data, int bits)
{
	int i;
	int c;
	
	for (c=0,i=0; i<bits; i++) {
		c |= (((data & (1<<i)) ? 1:0)) << (bits-1-i);
	}

	return c;
}

static int get_key_pcf8574(void* data, unsigned char* key, int key_no)
{
	struct i2c_ir *ir = data;
	int rc;
	unsigned char all, mask;

	/* compute all valid bits (key code + pressed/release flag) */
	all = ir->bits | ir->flag;

	/* save IR writable mask bits */
	mask = i2c_smbus_read_byte(&ir->client) & ~all;

	/* send bit mask */
	rc = i2c_smbus_write_byte(&ir->client, (0xff & all) | mask);

	/* receive scan code */
	rc = i2c_smbus_read_byte(&ir->client);

	if (rc == -1) {
		dprintk(DRIVER_NAME ": %s read error\n", ir->client.name);
		return -1;
	}

	/* drop duplicate polls */
	if (ir->b[0] == (rc & all)) {
		return -1;
	}
	ir->b[0] = rc & all;

	dprintk(DRIVER_NAME ": %s key 0x%02X %s\n",
		ir->client.name, rc & ir->bits,
		(rc & ir->flag) ? "released" : "pressed");

	if (rc & ir->flag) {
		/* ignore released buttons */
		return -1;
	}

	/* return valid key code */
	*key  = rc & ir->bits;
	return 0;
}

static int get_key_haup(void* data, unsigned char* key, int key_no)
{
	struct i2c_ir *ir = data;
        unsigned char buf[3];
	__u16 code;

	if (ir->nextkey != -1) {
		/* pass second byte */
		*key = ir->nextkey;
		ir->nextkey = -1;
		return 0;
	}

	/* poll IR chip */
	if (3 == i2c_master_recv(&ir->client,buf,3)) {
		ir->b[0] = buf[0];
		ir->b[1] = buf[1];
		ir->b[2] = buf[2];
	} else {
		dprintk(DRIVER_NAME ": read error\n");
		/* keep last successfull read buffer */
	}

	/* key pressed ? */
	if ((ir->b[0] & 0x80) == 0)
		return -1;
	
	dprintk(DRIVER_NAME ": key (0x%02x/0x%02x)\n",
	ir->b[0], ir->b[1]);

	/* look what we have */
	code = (((__u16)ir->b[0]&0x7f)<<6) | (ir->b[1]>>2);

	/* return it */
	*key        = (code >> 8) & 0xff;
	ir->nextkey =  code       & 0xff;
	return 0;
}

static int get_key_pixelview(void* data, unsigned char* key, int key_no)
{
	struct i2c_ir *ir = data;
        unsigned char b;
	
	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->client,&b,1)) {
		dprintk(DRIVER_NAME ": read error\n");
		return -1;
	}
	dprintk(DRIVER_NAME ": key %02x\n", b);
	*key = b;
	return 0;
}

static int get_key_pv951(void* data, unsigned char* key, int key_no)
{
	struct i2c_ir *ir = data;
        unsigned char b;
	static unsigned char codes[4];
	
	if(key_no>0)
	{
		if(key_no>=4) {
			dprintk(DRIVER_NAME
				": something wrong in get_key_pv951\n");
			return -EBADRQC;
		}
		*key = codes[key_no];
		return 0;
	}
	
	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->client,&b,1)) {
		dprintk(DRIVER_NAME ": read error\n");
		return -1;
	}
	/* ignore 0xaa */
	if (b==0xaa)
		return -1;
	dprintk(DRIVER_NAME ": key %02x\n", b);
	
	codes[2] = reverse(b,8);
	codes[3] = (~codes[2])&0xff;
	codes[0] = 0x61;
	codes[1] = 0xD6;
	
	*key=codes[0];
	return 0;
}

static int get_key_knc1(void *data, unsigned char *key, int key_no)
{
	struct i2c_ir *ir = data;
	unsigned char b;
	static unsigned char last_button = 0xFF;
	
	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->client,&b,1)) {
		dprintk(DRIVER_NAME ": read error\n");
		return -1;
	}
	
	/* it seems that 0xFE indicates that a button is still hold
	   down, while 0xFF indicates that no button is hold
	   down. 0xFE sequences are sometimes interrupted by 0xFF */
	
	if( b == 0xFF )
		return -1;
	
	dprintk(DRIVER_NAME ": key %02x\n", b);
	
	if ( b == 0xFE )
		b = last_button;

	*key = b;
	last_button = b;
	return 0;
}

static int set_use_inc(void* data)
{
	try_module_get(THIS_MODULE);
	return 0;
}

static void set_use_dec(void* data)
{
	module_put(THIS_MODULE);
}

static struct lirc_plugin lirc_template = {
	.name =        "lirc_i2c",
	.set_use_inc = set_use_inc,
	.set_use_dec = set_use_dec
};

/* ----------------------------------------------------------------------- */

static int lirc_i2c_attach(struct i2c_adapter *adap, int addr, int kind);
static int lirc_i2c_detach(struct i2c_client *client);
static int lirc_i2c_probe(struct i2c_adapter *adap);

static struct i2c_driver driver = {
	.owner          = THIS_MODULE,
        .name           = DRIVER_NAME,
        .id             = I2C_DRIVERID_EXP3, /* FIXME */
        .flags          = I2C_DF_NOTIFY,
        .attach_adapter = lirc_i2c_probe,
        .detach_client  = lirc_i2c_detach,
};

static struct i2c_client client_template = 
{
        I2C_DEVNAME("(unset)"),
	.flags  = I2C_CLIENT_ALLOW_USE,
        .driver = &driver
};

static int lirc_i2c_attach(struct i2c_adapter *adap, int addr, int kind)
{
        struct i2c_ir *ir;
	int ret;
	
        client_template.adapter = adap;
        client_template.addr = addr;
	
	if (NULL == (ir = kmalloc(sizeof(struct i2c_ir),GFP_KERNEL)))
                return -ENOMEM;
	memset(ir,0,sizeof(struct i2c_ir));
	memcpy(&ir->client,&client_template,sizeof(struct i2c_client));
        memcpy(&ir->lirc,&lirc_template,sizeof(struct lirc_plugin));
	
	ir->lirc.data		= ir;
	ir->lirc.minor		= minor;
	ir->nextkey		= -1;

	i2c_set_clientdata(&ir->client,ir);

	switch(addr)
	{
	case 0x64:
		strncpy(ir->client.name, "Pixelview IR", I2C_NAME_SIZE);
		ir->lirc.code_length = 8;
		ir->lirc.sample_rate = 10;
		ir->lirc.get_key = get_key_pixelview;
		break;
	case 0x4b:
		strncpy(ir->client.name,"PV951 IR", I2C_NAME_SIZE);
		ir->lirc.code_length = 32;
		ir->lirc.sample_rate = 10;
		ir->lirc.get_key = get_key_pv951;
		break;
	case 0x18:
	case 0x1a:
		strncpy(ir->client.name,"Hauppauge IR", I2C_NAME_SIZE);
		ir->lirc.code_length = 13;
		ir->lirc.sample_rate = 6;
		ir->lirc.get_key = get_key_haup;
		break;
	case 0x30:
		strncpy(ir->client.name,"KNC ONE IR", I2C_NAME_SIZE);
		ir->lirc.code_length = 8;
		ir->lirc.sample_rate = 10;
		ir->lirc.get_key = get_key_knc1;
		break;
	case 0x21:
	case 0x23:
		strncpy(ir->client.name,"TV-Box IR", I2C_NAME_SIZE);
		ir->lirc.code_length = 8;
		ir->lirc.sample_rate = 10;
		ir->lirc.get_key = get_key_pcf8574;
		ir->bits = ir->client.flags & 0xff;
		ir->flag = (ir->client.flags >> 8) & 0xff;
		break;
		
	default:
		/* shouldn't happen */
		dprintk(DRIVER_NAME ": unknown i2c address (0x%02x)?\n",addr);
		kfree(ir);
		return -1;
	}
	dprintk(DRIVER_NAME ": chip found @ 0x%02x (%s)\n",addr,
			ir->client.name);
	
	/* register device */
	i2c_attach_client(&ir->client);
	
	if((ret = lirc_register_plugin(&ir->lirc))) {
		dprintk(DRIVER_NAME ": device registration failed with %d\n",
				ret);
		kfree(ir);
		return -1;
	}

	ir->lirc.minor = ret;
	dprintk(DRIVER_NAME ": driver registered\n");

	return 0;
}

static int lirc_i2c_detach(struct i2c_client *client)
{
        struct i2c_ir *ir = i2c_get_clientdata(client);
	int err;
	
	/* unregister device */
	if ((err = lirc_unregister_plugin(ir->lirc.minor))) {
		dprintk(DRIVER_NAME ": lirc unregister failed\n");
		return err;
	} else {
		dprintk(DRIVER_NAME ": lirc unregister successful\n");
	}

	if ((err = i2c_detach_client(&ir->client))) {
		dprintk(DRIVER_NAME ": i2c detach failed\n");
		return err;
	} else {
		dprintk(DRIVER_NAME ": i2c detach successful\n");
	}

	/* free memory */
	kfree(ir);
	return 0;
}

static int lirc_i2c_probe(struct i2c_adapter *adap) {
	dprintk(DRIVER_NAME ": starting probe for adapter %s (0x%x)\n",
			adap->name, adap->id);
	return i2c_probe(adap, &addr_data, lirc_i2c_attach);
}

static int __init lirc_i2c_init(void)
{
	dprintk(DRIVER_NAME ": init\n");
	request_module("bttv");
	request_module("lirc_dev");
	return i2c_add_driver(&driver);
}

static void __exit lirc_i2c_exit(void)
{
	dprintk(DRIVER_NAME ": exit\n");
	i2c_del_driver(&driver);
}

module_init(lirc_i2c_init);
module_exit(lirc_i2c_exit);
