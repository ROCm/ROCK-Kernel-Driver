/*
    SMBus client for the Fujitsu Siemens Lifebook C-6535 Application Panel
    
    Copyright (C) 2001 Jochen Eisinger <jochen.eisinger@gmx.net>

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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/miscdevice.h>

/* Pulled what was needed from the userspace apanel.h file here */
/* Make sure they match what the userspace tools were build against */

/* Describes to which subsystem the ioctl applies. 'P' for proprietary? */
#define APANEL_IOCTL_GROUP      'P'



/*
 * Use one ioctl for all devices
 * This has the advantage that one interface is defined
 * for all devices. The caps can tell what is possible on
 * a certain device (with some defaults for each device
 * of course which the programmer doesn't have to check
 * explicitly like on/off for led and read for buttons).
 */

struct apanelcmd {
	int device;           /* Device to operate command on (APANEL_DEV_GEN_*) */
	int cmd;              /* Command to execute (APANEL_CMD_*) */
	int data;             /* Data for command */
};


struct fj_device {
	__u8	type;
	__u8	access_method;
	__u8	chip_type;
	__u8	number;
} __attribute__((packed));

#define DEVICE_APPLICATION_BUTTONS	1
#define DEVICE_CD_BUTTONS		2
#define DEVICE_LCD			3
#define DEVICE_LED_1			4
#define DEVICE_LED_2			6
#define DEVICE_APPLICATION_3_BUTTONS	7


#define IOCTL_APANEL_CMD        _IOR(APANEL_IOCTL_GROUP,202,struct apanelcmd)


/* Devices */
#define APANEL_DEV_LED          1
#define APANEL_DEV_LCD          2
#define APANEL_DEV_CDBTN        3
#define APANEL_DEV_APPBTN       4

/*
 * Commands
 */

/* Device capabilities */
#define APANEL_CAP_ONOFF        1       /* Turn on/off */
#define APANEL_CAP_GET          2       /* General get command */
#define APANEL_CAP_SET          4       /* General set command */
#define APANEL_CAP_BLINK        8       /* Blinking */
#define APANEL_CAP_DUTY         0x10    /* Duty cycle of blinking */
#define APANEL_CAP_RESET        0x20    /* Reset device */
#define APANEL_CAP_MAX          0x40    /* Get highest possible value for */
                                        /* set/get */
#define APANEL_CAP_MIN          0x80    /* Get lowest possible value */
#define APANEL_CMD_ONOFF        2
#define APANEL_ONOFF_ON         1       /* Or is APANEL_ON nicer? */
#define APANEL_ONOFF_OFF        0

#define APANEL_CMD_SET          3
#define APANEL_CMD_GET          4

#define APANEL_CMD_BLINK        5       /* data=Blink frequency *0.01Hz or so */
#define APANEL_CMD_DUTY         6       /* data=percentage high */

#define APANEL_CMD_RESET        7       /* If this is useful at all */

#define APANEL_CMD_MAX          8
#define APANEL_CMD_MIN          9

/*
 * Button masks
 */
/* Masks for application buttons */
#define APANEL_APPBTN_A         1
#define APANEL_APPBTN_B         2
#define APANEL_APPBTN_INTERNET  4
#define APANEL_APPBTN_EMAIL     8

/* Masks for cd buttons */
#define APANEL_CDBTN_STOP       1
#define APANEL_CDBTN_EJECT      2
#define APANEL_CDBTN_PLAY       4
#define APANEL_CDBTN_PAUSE      8
#define APANEL_CDBTN_BACK       0x10
#define APANEL_CDBTN_FORW       0x20



/* print lots of useless debug infos */
#define DEBUG

#define MY_NAME	"fjkeyinf"

#define dbg(format, arg...)					\
	do {							\
		if (debug)					\
			printk(KERN_DEBUG "%s: " format,	\
				MY_NAME , ## arg);		\
	} while (0)

static int debug = 1;

/*
 * this is the internal driver version number. There is no direct relationship
 * between the hardware, the apanel protocol, or the driver versions
 * However, this version number should be increased for every change
 */
/* current driver version */
#define FJKEYINF_VERSION_MAJOR	0
#define FJKEYINF_VERSION_MINOR	4

/*
 * This is the apanel version this driver implements. This _must_ be the
 * same as your apanel.h file
 */
/* protocol version */
#define FJKEYINF_APANEL_MAJOR	1
#define FJKEYINF_APANEL_MINOR	0

/*
 * Some real leet m4cr0s
 */
#define STRINGIFY1(x)	#x
#define STRINGIFY(x)	STRINGIFY1(x)

#define PASTE1(x,y)	x##y
#define PASTE(x,y)	PASTE1(x,y)

/*
 * This is the version of the driver as a string
 */
#define FJKEYINF_VERSION_STRING	PASTE("v",				\
				PASTE(STRINGIFY(FJKEYINF_VERSION_MAJOR),\
				PASTE(".",STRINGIFY(FJKEYINF_VERSION_MINOR))))

/*
 * This is the version of the protocol as a string
 */
#define FJKEYINF_APANEL_VERSION_STRING	PASTE("v",			\
					PASTE(STRINGIFY(FJKEYINF_APANEL_MAJOR),\
					PASTE(".",			\
					STRINGIFY(FJKEYINF_APANEL_MINOR))))

/* 
 * every i2c device has to register to the i2c sub-system. Therefor it needs
 * a driver id. We should be I2C_DRIVERID_FJKEYINF. However, if this isn't
 * defined in i2c-id.h, we'll use a generic id.
 */
#ifndef I2C_DRIVERID_FJKEYINF
#define I2C_DRIVERID_FJKEYINF	0xF000
#endif

/*
 * Yes, it's unbelievable, but me crappy driver has an official devices
 * entry. It's register as a misc-device (char-major-10) minor 216. The
 * location should be /dev/fujitsu/apanel... after all, it seems to be
 * a quite c00l driver ;>
 */
#define FJKEYINF_CHAR_MINOR	216

/*
 * Modules can store nice extra infos...
 */
MODULE_AUTHOR("Jochen Eisinger <jochen.eisinger@gmx.net>");
MODULE_DESCRIPTION("Application Panel driver for Lifebook C-series");
MODULE_LICENSE("GPL");

/*
 * So, let's start the hackin'
 *
 * we first define the interface for the i2c driver, cuz the misc device
 * part uses the struct fjkeyinf_client. also note that this struct is
 * static where it would be better to dynamically allocate one... but what
 * fsck, more than one apanel your crappy laptop won't have...
 */

/* definitions for i2c (smbus) interface */

/* forward declaration of the interface procedures */

/* detach removes the smbus driver from the system */
	static int fjkeyinf_detach(struct i2c_client *client);
/* attach tries to attach to a given smbus */
	static int fjkeyinf_attach(struct i2c_adapter *adap);

/* this structur defines the interface to the i2c sub-system */
static struct i2c_driver fjkeyinf_driver = {
	.owner		= THIS_MODULE,
	.name		= "fujitsu_panel" /* FJKEYINF_VERSION_STRING */,
	.id		= I2C_DRIVERID_FJKEYINF,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= &fjkeyinf_attach,
	.detach_client	= &fjkeyinf_detach,
	.command	= NULL,
};


/* Addresses to scan. afaik the device is at id 0x18. so we only scan this */
static unsigned short normal_i2c[] = {0x18,I2C_CLIENT_END};
static unsigned short normal_i2c_range[] = {I2C_CLIENT_END};

/* 
 * generate some stupid additional structures. we don't really care about
 * this
 */
I2C_CLIENT_INSMOD;


/*
 * If we've found the device, we have to provide an i2c_client structure
 * to the i2c subsystem with which this devices can be accessed. as I stated
 * above, there's max. 1 device, so I use _one static_ entry
 */
static struct i2c_client fjkeyinf_client = 
{
	.id =		-1,
	.flags =	0,
	.addr =		0,
	.adapter =	NULL,
	.driver =	&fjkeyinf_driver,
	.name =		"fjkeyinf",
};


/*
 * luckily, the c-series laptops have a configuration block in there BIOS.
 * so we can use it to detect the presence of the apanel device. There's
 * also a checksum somewhere, but we don't care about it.
 *
 * Note the first 8 characters. that's where this strange driver name comes
 * from.
 *
 * The configuration block can be found at 0x000FFA30
 *
 * There should also be an access method 3, but I don't know much about it.
 * Basically there should also be an LCD device. but my notebook hasn't one
 * so I don't know what the configuration block looks like
 *
 * type 1 is LED type 4 ist BUTTONS, probably 2 is LCD.
 */
static const unsigned char fjkeyinf_info[16] = {
	'F', 'J', 'K', 'E', 'Y', 'I', 'N', 'F',
	/* device 0 */	/* type		*/	1,
			/* access method*/	1,
			/* enabled	*/	1,
			/* smbus device	*/	0x30,
	/* device 1 */	/* type		*/	4,
			/* access method*/	1,
			/* enabled	*/	1,
			/* smbus device */	0x30};

#define FJKEYINF_INFO_ADDR	0x000FFA30
#define FJKEYINF_INFO_ADDR	0x000F6f20	/* Address for the P2120 */

/*
 * the following functions implement the ioctls. Note however, that not
 * much is implemented yet.
 */

/* turn a device on or off */

int fjkeyinf_onoff(int dev, int state)
{

	switch (dev) {

		case APANEL_DEV_LED:

			if (state) {
				dbg("turning LED on...\n");

				i2c_smbus_write_word_data(&fjkeyinf_client,
					       0x10, 0x8080);
				i2c_smbus_write_word_data(&fjkeyinf_client,
					       0x0f, 0x100);
				i2c_smbus_write_word_data(&fjkeyinf_client,
					       0x11, 1);

			} else {
				
				dbg("turning LED off...\n");

				i2c_smbus_write_word_data(&fjkeyinf_client,
					       0x10, 0);
				i2c_smbus_write_word_data(&fjkeyinf_client,
					       0x0f, 0x100);
				i2c_smbus_write_word_data(&fjkeyinf_client,
					       0x11, 0);


			}

			return state;

		default:

			printk(KERN_NOTICE
				"fjkeyinf: ONOFF called for invalid device"
				" (dev %d, state %d)\n", dev, state);

			return -EINVAL;

	}

}

/* gets the current value from a device */
int fjkeyinf_get(int dev)
{
	switch (dev) {
		case APANEL_DEV_LED:
			return ((i2c_smbus_read_word_data
					(&fjkeyinf_client, 0x00) >> 7) & 1);

		case APANEL_DEV_APPBTN:
			{
				int state;

				state = i2c_smbus_read_word_data
					(&fjkeyinf_client, 0x00);

				state = (state >> 8) & 0x0f;

				state ^= 0x0f;

				return (((state & 1) ? APANEL_APPBTN_EMAIL : 0)
					+ ((state & 2) ? APANEL_APPBTN_INTERNET : 0)
					+ ((state & 4) ? APANEL_APPBTN_A : 0)
					+ ((state & 8) ? APANEL_APPBTN_B : 0));
			}

		default:
			printk(KERN_NOTICE "fjkeyinf: GET called for invalid"
					" device (dev %d)\n", dev);
			return -EINVAL;
	}
}



/* 
 * file operations for /dev/fujitsu/apanel
 * 
 * That what the name says: file operation. pretty basic stuff follows
 */
			
/*
 * if somebody opens us, we just realize and let him pass
 */
static int fjkeyinf_open (struct inode *inode, struct file *filp)
{
	dbg("open called for /dev/fujitsu/apanel\n");
	return 0;
}

/* same with close */
static int fjkeyinf_close (struct inode *inode, struct file *filp)
{
	dbg("close called for /dev/fujitsu/apanel\n");
	return 0;
}

/* all commands are passed using the ioctl interface. so we have to decide
 * here, what we have to do */
int fjkeyinf_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		unsigned long args)
{
	struct apanelcmd *acmd;

	dbg("ioctl (cmd: %u, args: 0x%08lx)\n", cmd, args);

	/* Let's see, what they want from us... */
	
	switch (cmd) {
		case IOCTL_APANEL_CMD:
			/* the actual command is passed in a apanelcmd
			 * structure */

			if (!(args)) {

				printk(KERN_NOTICE "fjkeyinf: invalid apanel command "
						"(NULL pointer)\n");
				return -EINVAL;

			}

			acmd = (struct apanelcmd *)args;

			/* although not all commands are implemented, we
			 * understand all... */

			dbg("apanel command %d\n", acmd->cmd);


			switch (acmd->cmd) {
				case APANEL_CMD_ONOFF:
					return fjkeyinf_onoff(acmd->device,
							acmd->data);

				case APANEL_CMD_GET:
					return fjkeyinf_get(acmd->device);

				default:
					printk(KERN_NOTICE "fjkeyinf: unknown "
						"device/command %d/%d\n",
						acmd->device, acmd->cmd);
					return -EINVAL;
			}

		default:
			printk(KERN_NOTICE "fjkeyinf: unknown ioctl code %u\n", cmd);
			return -EINVAL;
	}
}

/* now we tell the misc_device what nice functions we've implemented */
static struct file_operations fjkeyinf_fops = {
	.owner =	THIS_MODULE,
	.ioctl =	fjkeyinf_ioctl,
	.open =		fjkeyinf_open,
	.release =	fjkeyinf_close,
};

/* misc dev entry. We need this to register to the misc dev driver */
static struct miscdevice fjkeyinf_dev = {
	FJKEYINF_CHAR_MINOR,
	"apanel",
	&fjkeyinf_fops
};


/* Now comes the i2c driver stuff. This is pretty straight forward, just as they
 * described it in their docu.
 */

/* basically this function should probe the i2c client, but we know that it has
 * to be the one we're looking for - and I have no idea how I should probe for
 * it, so we just register... */
static int fjkeyinf_probe(struct i2c_adapter *adap, int addr, int kind)
{
	int result;
        fjkeyinf_client.adapter = adap;
        fjkeyinf_client.addr = addr;

	dbg("%s\n", __FUNCTION__);
	if ((result = misc_register(&fjkeyinf_dev)) < 0) {
		printk(KERN_NOTICE "fjkeyinf: could not register misc device (%d)\n",
				result);
		return result;
	}

        i2c_attach_client(&fjkeyinf_client);

	printk(KERN_INFO "fjkeyinf: Application Panel Driver "
		       	/*FJKEYINF_VERSION_STRING*/ "\n");
	printk(KERN_INFO "fjkeyinf: Copyright (c) 2001 by "
		"Jochen Eisinger <jochen.eisinger@gmx.net>\n");

	
	dbg("driver loaded at address 0x%02x...\n", addr);
	
	return 0;
}


/* this function is invoked, when we should release our resource... */
static int fjkeyinf_detach(struct i2c_client *client)
{
	dbg("driver detached...\n");

	misc_deregister(&fjkeyinf_dev);
		
	i2c_detach_client(client);
	
	return 0;
}

/* this function is invoked for every i2c adapter, that has a device at the
 * address we've specified */
static int fjkeyinf_attach(struct i2c_adapter *adap)
{
	dbg("%s\n", __FUNCTION__);
	return i2c_probe(adap, &addr_data, fjkeyinf_probe);
}



/* startup */
static int __init fjkeyinf_init(void)
{
	unsigned char *fujitsu_bios = __va(FJKEYINF_INFO_ADDR);
//	int ctr;
	struct fj_device	*dev;
	
	if (__pa(high_memory) < (FJKEYINF_INFO_ADDR - 16)) {
		dbg("Fujitsu BIOS not found...\n");
		return -ENODEV;
	}

	dbg("Configuration block [%c%c%c%c%c%c%c%c] "
		"(%d, %d, %d, 0x%02x) (%d, %d, %d, 0x%02x)\n",
		fujitsu_bios[0],
		fujitsu_bios[1],
		fujitsu_bios[2],
		fujitsu_bios[3],
		fujitsu_bios[4],
		fujitsu_bios[5],
		fujitsu_bios[6],
		fujitsu_bios[7],
		(int)fujitsu_bios[8],
		(int)fujitsu_bios[9],
		(int)fujitsu_bios[10],
		(int)fujitsu_bios[11],
		(int)fujitsu_bios[12],
		(int)fujitsu_bios[13],
		(int)fujitsu_bios[14],
		(int)fujitsu_bios[15]);

	dev = (struct fj_device *)&fujitsu_bios[8];
	while (1) {
		if (dev->type == 0)
			break;
		dbg("type = %d, access_method = %d, chip_type = %d, number = %d\n",
		    dev->type, dev->access_method, dev->chip_type, dev->number);
		++dev;
	}
	
//	for (ctr=0 ; ctr<16 ; ctr++)
//		if (fujitsu_bios[ctr] != fjkeyinf_info[ctr]) {
//			dbg("device not found...\n");
//			return -ENODEV;
//		}

	dbg("device found...\n");
	
	i2c_add_driver(&fjkeyinf_driver);
	return 0;
}

static void __exit fjkeyinf_exit(void)
{
	i2c_del_driver(&fjkeyinf_driver);
}

module_init(fjkeyinf_init);
module_exit(fjkeyinf_exit);

