/* $Id: streamable.c,v 1.10 2000/02/05 06:47:30 ralf Exp $
 *
 * streamable.c: streamable devices. /dev/gfx
 * (C) 1997 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *
 * Major 10 is the streams clone device.  The IRIX Xsgi server just
 * opens /dev/gfx and closes it inmediately.
 *
 */

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include <asm/shmiq.h>
#include <asm/keyboard.h>
#include "graphics.h"


extern struct kbd_struct kbd_table [MAX_NR_CONSOLES];

/* console number where forwarding is enabled */
int forward_chars;

/* To which shmiq this keyboard is assigned */
int kbd_assigned_device;

/* previous kbd_mode for the forward_chars terminal */
int kbd_prev_mode;

/* Fetchs the strioctl information from user space for I_STR ioctls */
int
get_sioc (struct strioctl *sioc, unsigned long arg)
{
	int v;
	
	v = verify_area (VERIFY_WRITE, (void *) arg, sizeof (struct strioctl));
	if (v)
		return v;
	if (copy_from_user (sioc, (void *) arg, sizeof (struct strioctl)))
		return -EFAULT;
		
	v = verify_area (VERIFY_WRITE, (void *) sioc->ic_dp, sioc->ic_len);
	if (v)
		return v;
	return 0;
}

/* /dev/gfx device */
static int
sgi_gfx_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	printk ("GFX: ioctl 0x%x %ld called\n", cmd, arg);
	return 0;
	return -EINVAL;
}

struct file_operations sgi_gfx_fops = {
	ioctl:		sgi_gfx_ioctl,
};
 
static struct miscdevice dev_gfx = {
	SGI_GFX_MINOR, "sgi-gfx", &sgi_gfx_fops
};

/* /dev/input/keyboard streams device */
static idevDesc sgi_kbd_desc = {
        "keyboard",             /* devName */
        "KEYBOARD",             /* devType */
        240,                    /* nButtons */
        0,                      /* nValuators */
        0,                      /* nLEDs */
        0,                      /* nStrDpys */
        0,                      /* nIntDpys */
        0,                      /* nBells */
	IDEV_HAS_KEYMAP | IDEV_HAS_PCKBD
};

static int
sgi_kbd_sioc (idevInfo *dinfo, int cmd, int size, char *data, int *found)
{
	*found = 1;
	
	switch (cmd){

	case IDEVINITDEVICE:
		return 0;
			
	case IDEVGETDEVICEDESC:
		if (size >= sizeof (idevDesc)){
			if (copy_to_user (data, &sgi_kbd_desc, sizeof (sgi_kbd_desc)))
				return -EFAULT;
			return 0;
		}
		return -EINVAL;

	case IDEVGETKEYMAPDESC:
		if (size >= sizeof (idevKeymapDesc)){
			if (copy_to_user (data, "US", 3))
				return -EFAULT;
			return 0;
		}
		return -EINVAL;
	}
	*found = 0;
	return -EINVAL;
}

static int
sgi_keyb_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct strioctl sioc;
	int    f, v;
	
	/* IRIX calls I_PUSH on the opened device, go figure */
	if (cmd == I_PUSH)
		return 0;

	if (cmd == I_STR){
		v = get_sioc (&sioc, arg);
		if (v)
			return v;

		/* Why like this?  Because this is a sample piece of code
		 * that can be copied into other drivers and shows how to
		 * call a stock IRIX xxx_wioctl routine
		 *
		 * The NULL is supposed to be a idevInfo, right now we
		 * do not support this in our kernel.  
		 */
		return sgi_kbd_sioc (NULL, sioc.ic_cmd, sioc.ic_len, sioc.ic_dp, &f);
	}
	
	if (cmd == SHMIQ_ON){
		kbd_assigned_device = arg;
		forward_chars = fg_console + 1;
		kbd_prev_mode = kbd_table [fg_console].kbdmode;
		
	        kbd_table [fg_console].kbdmode = VC_RAW;
	} else if (cmd == SHMIQ_OFF && forward_chars){
		kbd_table [forward_chars-1].kbdmode = kbd_prev_mode;
		forward_chars = 0;
	} else
		return -EINVAL;
	return 0;
}

void
kbd_forward_char (int ch)
{
	static struct shmqevent ev;

	ev.data.flags  = (ch & 0200) ? 0 : 1;
	ev.data.which  = ch;
	ev.data.device = kbd_assigned_device + 0x11;
	shmiq_push_event (&ev);
}

static int
sgi_keyb_open (struct inode *inode, struct file *file)
{
	/* Nothing, but required by the misc driver */
	return 0;
}

struct file_operations sgi_keyb_fops = {
	ioctl:		sgi_keyb_ioctl,
	open:		sgi_keyb_open,
};

static struct miscdevice dev_input_keyboard = {
	SGI_STREAMS_KEYBOARD, "streams-keyboard", &sgi_keyb_fops
};

/* /dev/input/mouse streams device */
#define MOUSE_VALUATORS 2
static idevDesc sgi_mouse_desc = {
        "mouse",                /* devName */
        "MOUSE",                /* devType */
        3,                      /* nButtons */
        MOUSE_VALUATORS,	/* nValuators */
        0,                      /* nLEDs */
        0,                      /* nStrDpys */
        0,                      /* nIntDpys */
        0,                      /* nBells */
	0			/* flags */
};

static idevValuatorDesc mouse_default_valuator = {
	200,			/* hwMinRes */
        200,			/* hwMaxRes */
        0,			/* hwMinVal */
        65000,			/* hwMaxVal */
        IDEV_EITHER,		/* possibleModes */
        IDEV_ABSOLUTE,		/* default mode */
        200,			/* resolution */
        0,			/* minVal */
        65000			/* maxVal */
};

static int mouse_opened;
static idevValuatorDesc mouse_valuators [MOUSE_VALUATORS];
	
int
sgi_mouse_open (struct inode *inode, struct file *file)
{
	int i;
	
	if (mouse_opened)
		return -EBUSY;
	
	mouse_opened = 1;
	for (i = 0; i < MOUSE_VALUATORS; i++)
		mouse_valuators [i] = mouse_default_valuator;
	return 0;
}

static int
sgi_mouse_close (struct inode *inode, struct file *filp)
{
	lock_kernel();
	mouse_opened = 0;
	unlock_kernel();
	return 0;
}

static int
sgi_mouse_sioc (idevInfo *dinfo, int cmd, int size, char *data, int *found)
{
	*found = 1;

	switch (cmd){
	case IDEVINITDEVICE:
		return 0;
			
	case IDEVGETDEVICEDESC:
		if (size >= sizeof (idevDesc)){
			if (copy_to_user (data, &sgi_mouse_desc, sizeof (sgi_mouse_desc)))
				return -EFAULT;
			return 0;
		}
		return -EINVAL;

	case IDEVGETVALUATORDESC: {
		idevGetSetValDesc request, *ureq = (idevGetSetValDesc *) data;
		
		if (size < sizeof (idevGetSetValDesc))
			return -EINVAL;

		if (copy_from_user (&request, data, sizeof (request)))
			return -EFAULT;
		if (request.valNum >= MOUSE_VALUATORS)
			return -EINVAL;
		if (copy_to_user ((void *)&ureq->desc, 
				  (void *)&mouse_valuators [request.valNum],
				  sizeof (idevValuatorDesc)))
			return -EFAULT;
		return 0;
	}
	}
	*found = 0;
	return -EINVAL;
}

static int
sgi_mouse_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct strioctl sioc;
	int    f, v;

	/* IRIX calls I_PUSH on the opened device, go figure */
	switch (cmd){
	case I_PUSH:
		return 0;

	case I_STR:
		v = get_sioc (&sioc, arg);
		if (v)
			return v;
		
		/* Why like this?  Because this is a sample piece of code
		 * that can be copied into other drivers and shows how to
		 * call a stock IRIX xxx_wioctl routine
		 *
		 * The NULL is supposed to be a idevInfo, right now we
		 * do not support this in our kernel.  
		 */
		return sgi_mouse_sioc (NULL, sioc.ic_cmd, sioc.ic_len, sioc.ic_dp, &f);
		
	case SHMIQ_ON:
	case SHMIQ_OFF:
		return 0;
	}
	return 0;
}

struct file_operations sgi_mouse_fops = {
	ioctl:		sgi_mouse_ioctl,
	open:		sgi_mouse_open,
	release:	sgi_mouse_close,
};

/* /dev/input/mouse */
static struct miscdevice dev_input_mouse = {
	SGI_STREAMS_KEYBOARD, "streams-mouse", &sgi_mouse_fops
};

void
streamable_init (void)
{
	printk ("streamable misc devices registered (keyb:%d, gfx:%d)\n",
		SGI_STREAMS_KEYBOARD, SGI_GFX_MINOR);
	
	misc_register (&dev_gfx);
	misc_register (&dev_input_keyboard);
	misc_register (&dev_input_mouse);
}
