/* $Id: gpio.c,v 1.2 2001/12/18 13:35:15 bjornw Exp $
 *
 * Etrax general port I/O device
 *
 * Copyright (c) 1999, 2000, 2001 Axis Communications AB
 *
 * Authors:    Bjorn Wesen      (initial version)
 *             Ola Knutsson     (LED handling)
 *             Johan Adolfsson  (read/set directions, write)
 *
 * $Log: gpio.c,v $
 * Revision 1.2  2001/12/18 13:35:15  bjornw
 * Applied the 2.4.13->2.4.16 CRIS patch to 2.5.1 (is a copy of 2.4.15).
 *
 * Revision 1.12  2001/11/12 19:42:15  pkj
 * * Corrected return values from gpio_leds_ioctl().
 * * Fixed compiler warnings.
 *
 * Revision 1.11  2001/10/30 14:39:12  johana
 * Added D() around gpio_write printk.
 *
 * Revision 1.10  2001/10/25 10:24:42  johana
 * Added IO_CFG_WRITE_MODE ioctl and write method that can do fast
 * bittoggling in the kernel. (This speeds up programming an FPGA with 450kB
 * from ~60 seconds to 4 seconds).
 * Added save_flags/cli/restore_flags in ioctl.
 *
 * Revision 1.9  2001/05/04 14:16:07  matsfg
 * Corrected spelling error
 *
 * Revision 1.8  2001/04/27 13:55:26  matsfg
 * Moved initioremap.
 * Turns off all LEDS on init.
 * Added support for shutdown and powerbutton.
 *
 * Revision 1.7  2001/04/04 13:30:08  matsfg
 * Added bitset and bitclear for leds. Calls init_ioremap to set up memmapping
 *
 * Revision 1.6  2001/03/26 16:03:06  bjornw
 * Needs linux/config.h
 *
 * Revision 1.5  2001/03/26 14:22:03  bjornw
 * Namechange of some config options
 *
 * Revision 1.4  2001/02/27 13:52:48  bjornw
 * malloc.h -> slab.h
 *
 * Revision 1.3  2001/01/24 15:06:48  bjornw
 * gpio_wq correct type
 *
 * Revision 1.2  2001/01/18 16:07:30  bjornw
 * 2.4 port
 *
 * Revision 1.1  2001/01/18 15:55:16  bjornw
 * Verbatim copy of etraxgpio.c from elinux 2.0 added
 *
 *
 */

#include <linux/config.h>

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/poll.h>
#include <linux/init.h>

#include <asm/etraxgpio.h>
#include <asm/svinto.h>
#include <asm/io.h>
#include <asm/system.h>

#define GPIO_MAJOR 120  /* experimental MAJOR number */

#define D(x)

static char gpio_name[] = "etrax gpio";

#if 0
static wait_queue_head_t *gpio_wq;
#endif

static int gpio_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg);
static ssize_t gpio_write(struct file * file, const char * buf, size_t count,
                          loff_t *off);
static int gpio_open(struct inode *inode, struct file *filp);
static int gpio_release(struct inode *inode, struct file *filp);
static unsigned int gpio_poll(struct file *filp, struct poll_table_struct *wait);

/* private data per open() of this driver */

struct gpio_private {
	struct gpio_private *next;
	volatile unsigned char *port, *shadow;
	volatile unsigned char *dir, *dir_shadow;
	unsigned char changeable_dir;
	unsigned char changeable_bits;
	unsigned char highalarm, lowalarm;
	unsigned char clk_mask;
	unsigned char data_mask;
	unsigned char write_msb;
	wait_queue_head_t alarm_wq;
	int minor;
};

/* linked list of alarms to check for */

static struct gpio_private *alarmlist = 0;

#define NUM_PORTS 2
static volatile unsigned char *ports[2] = { R_PORT_PA_DATA, R_PORT_PB_DATA };
static volatile unsigned char *shads[2] = {
	&port_pa_data_shadow, &port_pb_data_shadow };

/* What direction bits that are user changeable 1=changeable*/
#ifndef CONFIG_ETRAX_PA_CHANGEABLE_DIR
#define CONFIG_ETRAX_PA_CHANGEABLE_DIR 0x00
#endif
#ifndef CONFIG_ETRAX_PB_CHANGEABLE_DIR
#define CONFIG_ETRAX_PB_CHANGEABLE_DIR 0x00
#endif

#ifndef CONFIG_ETRAX_PA_CHANGEABLE_BITS
#define CONFIG_ETRAX_PA_CHANGEABLE_BITS 0xFF
#endif
#ifndef CONFIG_ETRAX_PB_CHANGEABLE_BITS
#define CONFIG_ETRAX_PB_CHANGEABLE_BITS 0xFF
#endif


static unsigned char changeable_dir[2] = { CONFIG_ETRAX_PA_CHANGEABLE_DIR,
                                           CONFIG_ETRAX_PB_CHANGEABLE_DIR };
static unsigned char changeable_bits[2] = { CONFIG_ETRAX_PA_CHANGEABLE_BITS,
                                            CONFIG_ETRAX_PB_CHANGEABLE_BITS };

static volatile unsigned char *dir[2] = { R_PORT_PA_DIR, R_PORT_PB_DIR };

static volatile unsigned char *dir_shadow[2] = {
	&port_pa_dir_shadow, &port_pb_dir_shadow };

#define LEDS 2

static unsigned int 
gpio_poll(struct file *filp,
	  struct poll_table_struct *wait)
{
	/* TODO poll on alarms! */
#if 0
        if (!ANYTHING_WANTED) {
		D(printk("gpio_select sleeping task\n"));
	        select_wait(&gpio_wq, table);
	        return 0;
	}
	D(printk("gpio_select ready\n"));
#endif
	return 1;
}

static ssize_t gpio_write(struct file * file, const char * buf, size_t count,
                          loff_t *off)
{
	struct gpio_private *priv = (struct gpio_private *)file->private_data;
	unsigned char data, clk_mask, data_mask, write_msb;
	unsigned long flags;
	ssize_t retval = count;
	if (verify_area(VERIFY_READ, buf, count)) {
		return -EFAULT;
	}
	clk_mask = priv->clk_mask;
	data_mask = priv->data_mask;
	/* It must have been configured using the IO_CFG_WRITE_MODE */
	/* Perhaps a better error code? */
	if (clk_mask == 0 || data_mask == 0) {
		return -EPERM;
	}
	write_msb = priv->write_msb;
	D(printk("gpio_write: %lu to data 0x%02X clk 0x%02X msb: %i\n",count, data_mask, clk_mask, write_msb));
	while (count--) {
		int i;
		data = *buf++;
		if (priv->write_msb) {
			for (i = 7; i >= 0;i--) {
				save_flags(flags); cli();
				*priv->port = *priv->shadow &= ~clk_mask;
				if (data & 1<<i)
					*priv->port = *priv->shadow |= data_mask;
				else
					*priv->port = *priv->shadow &= ~data_mask;
			/* For FPGA: min 5.0ns (DCC) before CCLK high */
				*priv->port = *priv->shadow |= clk_mask;
				restore_flags(flags);
			}
		} else {
			for (i = 0; i <= 7;i++) {
				save_flags(flags); cli();
				*priv->port = *priv->shadow &= ~clk_mask;
				if (data & 1<<i)
					*priv->port = *priv->shadow |= data_mask;
				else
					*priv->port = *priv->shadow &= ~data_mask;
			/* For FPGA: min 5.0ns (DCC) before CCLK high */
				*priv->port = *priv->shadow |= clk_mask;
				restore_flags(flags);
			}
		}
	}
	return retval;
}

static int
gpio_open(struct inode *inode, struct file *filp)
{
	struct gpio_private *priv;
	int p = minor(inode->i_rdev);

	if (p >= NUM_PORTS && p != LEDS)
		return -EINVAL;

	priv = (struct gpio_private *)kmalloc(sizeof(struct gpio_private), 
					      GFP_KERNEL);

	if (!priv)
		return -ENOMEM;

	priv->minor = p;

	/* initialize the io/alarm struct and link it into our alarmlist */

	priv->next = alarmlist;
	alarmlist = priv;
	priv->port = ports[p];
	priv->shadow = shads[p];

	priv->changeable_dir = changeable_dir[p];
	priv->changeable_bits = changeable_bits[p];
	priv->dir = dir[p];
	priv->dir_shadow = dir_shadow[p];

	priv->highalarm = 0;
	priv->lowalarm = 0;
	priv->clk_mask = 0;
	priv->data_mask = 0;
	init_waitqueue_head(&priv->alarm_wq);

	filp->private_data = (void *)priv;

	return 0;
}

static int
gpio_release(struct inode *inode, struct file *filp)
{
	struct gpio_private *p = alarmlist;
	struct gpio_private *todel = (struct gpio_private *)filp->private_data;

	/* unlink from alarmlist and free the private structure */

	if (p == todel) {
		alarmlist = todel->next;
	} else {
		while (p->next != todel)
			p = p->next;
		p->next = todel->next;
	}

	kfree(todel);

	return 0;
}

/* Main device API. ioctl's to read/set/clear bits, as well as to 
 * set alarms to wait for using a subsequent select().
 */

static int
gpio_leds_ioctl(unsigned int cmd, unsigned long arg);

static int
gpio_ioctl(struct inode *inode, struct file *file,
	   unsigned int cmd, unsigned long arg)
{
	unsigned long flags;
	struct gpio_private *priv = (struct gpio_private *)file->private_data;
	if (_IOC_TYPE(cmd) != ETRAXGPIO_IOCTYPE) {
		return -EINVAL;
	}

	switch (_IOC_NR(cmd)) {
		case IO_READBITS:
			// read the port
			return *priv->port;
		case IO_SETBITS:
			save_flags(flags); cli();
			// set changeable bits with a 1 in arg
			*priv->port = *priv->shadow |= 
			  ((unsigned char)arg & priv->changeable_bits);
			restore_flags(flags);
			break;
		case IO_CLRBITS:
			save_flags(flags); cli();
			// clear changeable bits with a 1 in arg
			*priv->port = *priv->shadow &= 
			  ~((unsigned char)arg & priv->changeable_bits);
			restore_flags(flags);
			break;
		case IO_HIGHALARM:
			// set alarm when bits with 1 in arg go high
			priv->highalarm |= (unsigned char)arg;
			break;
		case IO_LOWALARM:
			// set alarm when bits with 1 in arg go low
			priv->lowalarm |= (unsigned char)arg;
			break;
		case IO_CLRALARM:
			// clear alarm for bits with 1 in arg
			priv->highalarm &= ~(unsigned char)arg;
			priv->lowalarm  &= ~(unsigned char)arg;
			break;
		case IO_READDIR:
			/* Read direction 0=input 1=output */
			return *priv->dir_shadow;
		case IO_SETINPUT:
			save_flags(flags); cli();
			/* Set direction 0=unchanged 1=input */
			*priv->dir = *priv->dir_shadow &= 
			~((unsigned char)arg & priv->changeable_dir);
			restore_flags(flags);
			return *priv->dir_shadow;
		case IO_SETOUTPUT:
			save_flags(flags); cli();
			/* Set direction 0=unchanged 1=output */
			*priv->dir = *priv->dir_shadow |= 
			  ((unsigned char)arg & priv->changeable_dir);
			restore_flags(flags);
			return *priv->dir_shadow;
                case IO_SHUTDOWN:
                       SOFT_SHUTDOWN();
                       break;
                case IO_GET_PWR_BT:
#if defined (CONFIG_ETRAX_SOFT_SHUTDOWN)
                       return (*R_PORT_G_DATA & 
                               ( 1 << CONFIG_ETRAX_POWERBUTTON_BIT));
#else
                       return 0;
#endif
			break;
		case IO_CFG_WRITE_MODE:
			priv->clk_mask = arg & 0xFF;
			priv->data_mask = (arg >> 8) & 0xFF;
			priv->write_msb = (arg >> 16) & 0x01;
			/* Check if we're allowed to change the bits and
			 * the direction is correct
			 */
			if (!((priv->clk_mask & priv->changeable_bits) &&
			      (priv->data_mask & priv->changeable_bits) &&
			      (priv->clk_mask & *priv->dir_shadow) &&
			      (priv->data_mask & *priv->dir_shadow)))
			{
				priv->clk_mask = 0;
				priv->data_mask = 0;
				return -EPERM;
			}
			break;
		default:
			if (priv->minor == LEDS)
				return gpio_leds_ioctl(cmd, arg);
                        else
				return -EINVAL;
	}
	
	return 0;
}

static int
gpio_leds_ioctl(unsigned int cmd, unsigned long arg)
{
	unsigned char green;
	unsigned char red;

	switch (_IOC_NR(cmd)) {
		case IO_LEDACTIVE_SET:
			green = ((unsigned char) arg) & 1;
			red   = (((unsigned char) arg) >> 1) & 1;
			LED_ACTIVE_SET_G(green);
			LED_ACTIVE_SET_R(red);
			break;

		case IO_LED_SETBIT:                 
			LED_BIT_SET(arg);
			break;

		case IO_LED_CLRBIT:
			LED_BIT_CLR(arg);
			break;

		default:
			return -EINVAL;
	}

	return 0;
}

struct file_operations gpio_fops = {
	.owner       = THIS_MODULE,
	.poll        = gpio_poll,
	.ioctl       = gpio_ioctl,
	.write       = gpio_write,
	.open        = gpio_open,
	.release     = gpio_release,
};

/* main driver initialization routine, called from mem.c */

static __init int
gpio_init(void)
{
	extern void init_ioremap(void);
	int res;
#if defined (CONFIG_ETRAX_CSP0_LEDS)
	int i;
#endif

	/* do the formalities */

	res = register_chrdev(GPIO_MAJOR, gpio_name, &gpio_fops);
	if (res < 0) {
		printk(KERN_ERR "gpio: couldn't get a major number.\n");
		return res;
	}

        /* Clear all leds */
#if defined (CONFIG_ETRAX_CSP0_LEDS) ||  defined (CONFIG_ETRAX_PA_LEDS) || defined (CONFIG_ETRAX_PB_LEDS) 
	init_ioremap();
	LED_NETWORK_SET(0);
	LED_ACTIVE_SET(0);
	LED_DISK_READ(0);
	LED_DISK_WRITE(0);        

#if defined (CONFIG_ETRAX_CSP0_LEDS)
	for (i = 0; i < 32; i++) {
		LED_BIT_SET(i);
	}
#endif

#endif
	
	printk("ETRAX 100LX GPIO driver v2.2, (c) 2001 Axis Communications AB\n");

	return res;
}

/* this makes sure that gpio_init is called during kernel boot */

module_init(gpio_init);
