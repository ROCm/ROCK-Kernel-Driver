/*      $Id: lirc_i2c.c,v 1.26 2004/09/05 16:48:48 lirc Exp $      */

/*
 * i2c IR lirc plugin for Hauppauge and Pixelview cards - new 2.3.x i2c stack
 *
 * Copyright (c) 2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 * modified for PixelView (BT878P+W/FM) by
 *      Michal Kochanowicz <mkochano@pld.org.pl>
 *      Christoph Bartelmus <lirc@bartelmus.de>
 * modified for KNC ONE TV Station/Anubis Typhoon TView Tuner by
 *      Ulrich Mueller <ulrich.mueller42@web.de>
 * modified for Asus TV-Box and Creative/VisionTek BreakOut-Box by
 *      Stefan Jahn <stefan@lkcc.org>
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

#include <linux/version.h>
#if LINUX_VERSION_CODE < 0x020200
#error "--- Sorry, this driver needs kernel version 2.2.0 or higher. ---"
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#ifndef I2C_CLIENT_END
#error "********************************************************"
#error " Sorry, this driver needs the new I2C stack.            "
#error " You can get it at http://www2.lm-sensors.nu/~lm78/.    "
#error "********************************************************"
#endif

#include <linux/i2c-algo-bit.h>

#include <asm/semaphore.h>

#include "kcompat.h"
#include "lirc_dev.h"
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#include "../drivers/char/bttv.h"
#else
#include "../drivers/media/video/bttv.h"
#endif

struct IR {
	struct lirc_plugin l;
	struct i2c_client  c;
	int nextkey;
	unsigned char b[3];
	unsigned char bits;
	unsigned char flag;
};

/* ----------------------------------------------------------------------- */

#define DEVICE_NAME "lirc_i2c"

/* ----------------------------------------------------------------------- */
/* insmod parameters                                                       */

static int debug   = 0;    /* debug output */
static int minor   = -1;   /* minor number */

#define dprintk(fmt, args...)                                           \
	do{                                                             \
		if(debug) printk(KERN_DEBUG DEVICE_NAME ": " fmt,       \
				 ## args);                              \
	}while(0)

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

static int add_to_buf_pcf8574(void* data, struct lirc_buffer* buf)
{
	struct IR *ir = data;
	int rc;
	unsigned char all, mask;
	unsigned char key;

	/* compute all valid bits (key code + pressed/release flag) */
	all = ir->bits | ir->flag;

	/* save IR writable mask bits */
	mask = i2c_smbus_read_byte(&ir->c) & ~all;

	/* send bit mask */
	rc = i2c_smbus_write_byte(&ir->c, (0xff & all) | mask);

	/* receive scan code */
	rc = i2c_smbus_read_byte(&ir->c);

	if (rc == -1) {
		dprintk("%s read error\n", ir->c.name);
		return -EIO;
	}

	/* drop duplicate polls */
	if (ir->b[0] == (rc & all)) {
		return -ENODATA;
	}
	ir->b[0] = rc & all;

	dprintk("%s key 0x%02X %s\n", ir->c.name, rc & ir->bits,
		(rc & ir->flag) ? "released" : "pressed");

	if (rc & ir->flag) {
		/* ignore released buttons */
		return -ENODATA;
	}

	/* set valid key code */
	key  = rc & ir->bits;
	lirc_buffer_write_1( buf, &key );
	return 0;
}

static int add_to_buf_haup(void* data, struct lirc_buffer* buf)
{
	struct IR *ir = data;
	unsigned char keybuf[3];
	__u16 code;
	unsigned char codes[2];

	/* poll IR chip */
	if (3 == i2c_master_recv(&ir->c,keybuf,3)) {
		ir->b[0] = keybuf[0];
		ir->b[1] = keybuf[1];
		ir->b[2] = keybuf[2];
		dprintk("key (0x%02x/0x%02x)\n", ir->b[0], ir->b[1]);
	} else {
		dprintk("read error\n");
		/* keep last successfull read buffer */
	}

	/* key pressed ? */
	if ((ir->b[0] & 0x80) == 0)
		return -ENODATA;
	
	/* look what we have */
	code = (((__u16)ir->b[0]&0x7f)<<6) | (ir->b[1]>>2);
	
	codes[0] = (code >> 8) & 0xff;
	codes[1] = code & 0xff;

	/* return it */
	lirc_buffer_write_1( buf, codes );
	return 0;
}

static int add_to_buf_pixelview(void* data, struct lirc_buffer* buf)
{
	struct IR *ir = data;
	unsigned char key;
	
	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->c,&key,1)) {
		dprintk("read error\n");
		return -1;
	}
	dprintk("key %02x\n", key);

	/* return it */
	lirc_buffer_write_1( buf, &key );
	return 0;
}

static int add_to_buf_pv951(void* data, struct lirc_buffer* buf)
{
	struct IR *ir = data;
	unsigned char key;
	unsigned char codes[4];
	
	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->c,&key,1)) {
		dprintk("read error\n");
		return -ENODATA;
	}
	/* ignore 0xaa */
	if (key==0xaa)
		return -ENODATA;
	dprintk("key %02x\n", key);

	codes[0] = 0x61;
	codes[1] = 0xD6;
	codes[2] = reverse(key,8);
	codes[3] = (~codes[2])&0xff;
	
	lirc_buffer_write_1( buf, codes );
	return 0;
}

static int add_to_buf_knc1(void *data, struct lirc_buffer* buf)
{
	static unsigned char last_key = 0xFF;
	struct IR *ir = data;
	unsigned char key;
	
	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->c,&key,1)) {
		dprintk("read error\n");
		return -ENODATA;
	}
	
	/* it seems that 0xFE indicates that a button is still hold
	   down, while 0xFF indicates that no button is hold
	   down. 0xFE sequences are sometimes interrupted by 0xFF */
	
	dprintk("key %02x\n", key);
	
	if( key == 0xFF )
		return -ENODATA;
	
	if ( key == 0xFE )
		key = last_key;

	last_key = key;
	lirc_buffer_write_1( buf, &key );

	return 0;
}

static int set_use_inc(void* data)
{
	struct IR *ir = data;

	/* lock bttv in memory while /dev/lirc is in use  */
	/* this is completely broken code. lirc_unregister_plugin()
	   must be possible even when the device is open */
#ifdef KERNEL_2_5
	i2c_use_client(&ir->c);
#else
	if (ir->c.adapter->inc_use) 
		ir->c.adapter->inc_use(ir->c.adapter);
#endif

	MOD_INC_USE_COUNT;
	return 0;
}

static void set_use_dec(void* data)
{
	struct IR *ir = data;

#ifdef KERNEL_2_5
	i2c_release_client(&ir->c);
#else
	if (ir->c.adapter->dec_use) 
		ir->c.adapter->dec_use(ir->c.adapter);
#endif
	MOD_DEC_USE_COUNT;
}

static struct lirc_plugin lirc_template = {
	name:        "lirc_i2c",
	set_use_inc: set_use_inc,
	set_use_dec: set_use_dec
};

/* ----------------------------------------------------------------------- */

static int ir_attach(struct i2c_adapter *adap, int addr,
		      unsigned short flags, int kind);
static int ir_detach(struct i2c_client *client);
static int ir_probe(struct i2c_adapter *adap);
static int ir_command(struct i2c_client *client, unsigned int cmd, void *arg);

static struct i2c_driver driver = {
        name:           "i2c ir driver",
        id:             I2C_DRIVERID_EXP3, /* FIXME */
        flags:          I2C_DF_NOTIFY,
        attach_adapter: ir_probe,
        detach_client:  ir_detach,
        command:        ir_command,
};

static struct i2c_client client_template = 
{
        name:   "unset",
        driver: &driver
};

static int ir_attach(struct i2c_adapter *adap, int addr,
		     unsigned short flags, int kind)
{
        struct IR *ir;
	
        client_template.adapter = adap;
        client_template.addr = addr;
	
        if (NULL == (ir = kmalloc(sizeof(struct IR),GFP_KERNEL)))
                return -ENOMEM;
        memcpy(&ir->l,&lirc_template,sizeof(struct lirc_plugin));
        memcpy(&ir->c,&client_template,sizeof(struct i2c_client));
	
	ir->c.adapter = adap;
	ir->c.addr    = addr;
#ifdef KERNEL_2_5
	i2c_set_clientdata(&ir->c, ir);
#else
	ir->c.data    = ir;
#endif
	ir->l.data    = ir;
	ir->l.minor   = minor;
	ir->l.sample_rate = 10;
	ir->nextkey   = -1;

	switch(addr)
	{
	case 0x64:
		strcpy(ir->c.name,"Pixelview IR");
		ir->l.code_length = 8;
		ir->l.add_to_buf=add_to_buf_pixelview;
		break;
	case 0x4b:
		strcpy(ir->c.name,"PV951 IR");
		ir->l.code_length = 32;
		ir->l.add_to_buf=add_to_buf_pv951;
		break;
	case 0x18:
	case 0x1a:
		strcpy(ir->c.name,"Hauppauge IR");
		ir->l.code_length = 13;
		ir->l.add_to_buf=add_to_buf_haup;
		break;
	case 0x30:
		strcpy(ir->c.name,"KNC ONE IR");
		ir->l.code_length = 8;
		ir->l.add_to_buf=add_to_buf_knc1;
		break;
	case 0x21:
	case 0x23:
		strcpy(ir->c.name,"TV-Box IR");
		ir->l.code_length = 8;
		ir->l.add_to_buf=add_to_buf_pcf8574;
		ir->bits = flags & 0xff;
		ir->flag = (flags >> 8) & 0xff;
		break;
		
	default:
		/* shouldn't happen */
		printk("lirc_i2c: Huh? unknown i2c address (0x%02x)?\n",addr);
		kfree(ir);
		return -1;
	}
	printk("lirc_i2c: chip found @ 0x%02x (%s)\n",addr,ir->c.name);
	
	/* register device */
	i2c_attach_client(&ir->c);
	ir->l.minor = lirc_register_plugin(&ir->l);
#ifdef KERNEL_2_5
	i2c_use_client(&ir->c);
#else
	if (ir->c.adapter->inc_use) 
		ir->c.adapter->inc_use(ir->c.adapter);
#endif
	
	return 0;
}

static int ir_detach(struct i2c_client *client)
{
#ifdef KERNEL_2_5
	struct IR *ir = i2c_get_clientdata(client);
#else
        struct IR *ir = client->data;
#endif
	
	/* unregister device */
#ifdef KERNEL_2_5
	i2c_release_client(&ir->c);
#else
	if (ir->c.adapter->dec_use) 
		ir->c.adapter->dec_use(ir->c.adapter);
#endif
	lirc_unregister_plugin(ir->l.minor);
	i2c_detach_client(&ir->c);

	/* free memory */
	kfree(ir);
	return 0;
}

static int ir_probe(struct i2c_adapter *adap) {
	
	/* The external IR receiver is at i2c address 0x34 (0x35 for
	   reads).  Future Hauppauge cards will have an internal
	   receiver at 0x30 (0x31 for reads).  In theory, both can be
	   fitted, and Hauppauge suggest an external overrides an
	   internal. 
	   
	   That's why we probe 0x1a (~0x34) first. CB 
	*/
	
	static const int probe[] = { 0x1a, 0x18, 0x4b, 0x64, 0x30, -1};
	struct i2c_client c; char buf; int i,rc;

	if (adap->id == (I2C_ALGO_BIT | I2C_HW_B_BT848)) {
		memset(&c,0,sizeof(c));
		c.adapter = adap;
		for (i = 0; -1 != probe[i]; i++) {
			c.addr = probe[i];
			rc = i2c_master_recv(&c,&buf,1);
			dprintk("probe 0x%02x @ %s: %s\n",
				probe[i], adap->name, 
				(1 == rc) ? "yes" : "no");
			if (1 == rc)
			{
				ir_attach(adap,probe[i],0,0);
			}
		}
	}

	/* Asus TV-Box and Creative/VisionTek BreakOut-Box (PCF8574) */
	else if (adap->id == (I2C_ALGO_BIT | I2C_HW_B_RIVA)) {
		/* addresses to probe;
		   leave 0x24 and 0x25 because SAA7113H possibly uses it 
		   0x21 and 0x22 possibly used by SAA7108E 
		   Asus:      0x21 is a correct address (channel 1 of PCF8574)
		   Creative:  0x23 is a correct address (channel 3 of PCF8574)
		   VisionTek: 0x23 is a correct address (channel 3 of PCF8574)
		*/
		static const int pcf_probe[] = { 0x20, 0x21, 0x22, 0x23,
						 0x24, 0x25, 0x26, 0x27, -1 };
		int ret1, ret2, ret3, ret4;
		unsigned char bits = 0, flag = 0;

		memset(&c,0,sizeof(c));
		c.adapter = adap;
		for (i = 0; -1 != pcf_probe[i]; i++) {
			c.addr = pcf_probe[i];
			ret1 = i2c_smbus_write_byte(&c, 0xff);
			ret2 = i2c_smbus_read_byte(&c);
			ret3 = i2c_smbus_write_byte(&c, 0x00);
			ret4 = i2c_smbus_read_byte(&c);

			/* ensure that the writable bitmask works correctly */
			rc = 0;
			if (ret1 != -1 && ret2 != -1 && 
			    ret3 != -1 && ret4 != -1) {
				/* in the Asus TV-Box: bit 1-0 */
				if (((ret2 & 0x03) == 0x03) && 
				    ((ret4 & 0x03) == 0x00)) {
					bits = (unsigned char) ~0x07;
					flag = 0x04;
					rc = 1;
				}
				/* in the Creative/VisionTek BreakOut-Box: bit 7-6 */
				if (((ret2 & 0xc0) == 0xc0) && 
				    ((ret4 & 0xc0) == 0x00)) {
					bits = (unsigned char) ~0xe0;
					flag = 0x20;
					rc = 1;
				}
			}
			dprintk("probe 0x%02x @ %s: %s\n",
				c.addr, adap->name, rc ? "yes" : "no");
			if (rc)
				ir_attach(adap,pcf_probe[i],bits|(flag<<8),0);
		}
	}
		
	return 0;
}

static int ir_command(struct i2c_client *client,unsigned int cmd, void *arg)
{
	/* nothing */
	return 0;
}

/* ----------------------------------------------------------------------- */
#ifdef MODULE

int init_module(void)
{
	request_module("bttv");
	request_module("rivatv");
	i2c_add_driver(&driver);
	return 0;
}

void cleanup_module(void)
{
	i2c_del_driver(&driver);
}

MODULE_DESCRIPTION("Infrared receiver driver for Hauppauge and Pixelview cards (i2c stack)");
MODULE_AUTHOR("Gerd Knorr, Michal Kochanowicz, Christoph Bartelmus, Ulrich Mueller, Stefan Jahn");
MODULE_LICENSE("GPL");

module_param(minor, int, 0444);
MODULE_PARM_DESC(minor, "Preferred minor device number");

module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable debugging messages");

EXPORT_NO_SYMBOLS;

#endif /* MODULE */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
