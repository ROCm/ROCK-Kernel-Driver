/*
 *
 * Vista Imaging ViCAM / 3Com HomeConnect Usermode Driver
 * Christopher L Cheney (C) 2001
 * 
 */

#ifndef __LINUX_VICAM_H
#define __LINUX_VICAM_H


#ifdef CONFIG_USB_DEBUG
	static int debug = 1;
#else
	static int debug;
#endif

/* Use our own dbg macro */
#undef dbg
#define dbg(format, arg...) do { if (debug) printk(KERN_DEBUG __FILE__ ": " format "\n" , ## arg); } while (0)

#define VICAM_NUMFRAMES 30
#define VICAM_NUMSBUF 1

/* USB REQUEST NUMBERS */
#define VICAM_REQ_VENDOR	0xff
#define VICAM_REQ_CAMERA_POWER	0x50
#define VICAM_REQ_CAPTURE	0x51
#define VICAM_REQ_LED_CONTROL	0x55
#define VICAM_REQ_GET_SOMETHIN	0x56
 
/* not required but lets you know camera is on */
/* camera must be on to turn on led */
/* 0x01 always on  0x03 on when picture taken (flashes) */

struct picture_parm
{
	int width;
	int height;
	int brightness;
	int hue;
	int colour;
	int contrast;
	int whiteness;
	int depth;
	int palette;
};

struct vicam_scratch {
        unsigned char *data;
        volatile int state;
        int offset;
        int length;
};

/* Structure to hold all of our device specific stuff */
struct usb_vicam
{
	struct video_device vdev;
	struct usb_device *udev;

	int open_count;	/* number of times this port has been opened */
	struct semaphore sem;			/* locks this structure */
	wait_queue_head_t wait;			/* Processes waiting */ 

	int streaming;

	/* v4l stuff */
	char *camera_name;
	char *fbuf;
	urb_t *urb[VICAM_NUMSBUF];
	int sizes;
	int *width;
	int *height;
	int maxframesize;
	struct picture_parm win;
	struct proc_dir_entry *proc_entry;      /* /proc/se401/videoX */
	struct urb readurb;
};

#endif
